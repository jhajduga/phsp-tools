"""Generate downloadable citations and the README citation block from CITATION.cff."""

import argparse
import re
import subprocess
import sys
from pathlib import Path

import yaml

START = "<!-- CITATION:START -->"
END = "<!-- CITATION:END -->"


def update(root, check=False):
    source = root / "CITATION.cff"
    metadata = yaml.safe_load(source.read_text())
    version = str(metadata["version"])
    cmake = re.search(r"project\(phsp_tools\s+VERSION\s+(\S+)", (root / "CMakeLists.txt").read_text())
    if not cmake or cmake[1] != version:
        raise ValueError("CMakeLists.txt and CITATION.cff must declare the same version")
    executable = Path(sys.executable).with_name("cffconvert")
    subprocess.run([str(executable), "--validate", "--infile", str(source)], check=True)

    def convert(format):
        return subprocess.check_output(
            [str(executable), "--format", format, "--infile", str(source)], text=True
        ).strip()

    text = convert("apalike") + "\n"
    bibtex = convert("bibtex")
    key = "phsp_tools_" + re.sub(r"[^A-Za-z0-9]", "_", version)
    bibtex = re.sub(r"(@\w+\{)[^,]+,", rf"\g<1>{key},", bibtex, count=1)
    # cffconvert 2.0.0 omits the version in BibTeX; note also works with classic styles.
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise ValueError("Expected a release version in X.Y.Z format")
    bibtex = bibtex.replace("\n", f"\nversion = {{{version}}},\nnote = {{Version {version}}},\n", 1) + "\n"
    repository = metadata["repository-code"].rstrip("/")
    raw = repository.replace("https://github.com/", "https://raw.githubusercontent.com/") + "/main"
    block = (
        f"{START}\n"
        "Use **Cite this repository** on the GitHub repository page to copy a citation,\n"
        f"download [BibTeX]({raw}/CITATION.bib) or [plain text]({raw}/CITATION.txt),\n"
        "or copy one of the references below. Metadata are maintained in [CITATION.cff](CITATION.cff).\n\n"
        "### Plain-text reference\n\n"
        f"```text\n{text.rstrip()}\n```\n\n"
        "### BibTeX\n\n"
        f"```bibtex\n{bibtex.rstrip()}\n```\n\n"
    )
    if metadata.get("doi"):
        doi = metadata["doi"]
        block += f"[![DOI](https://zenodo.org/badge/DOI/{doi}.svg)](https://doi.org/{doi})\n\n"
    if not metadata.get("doi"):
        block += (
            "A Zenodo DOI is not yet recorded in `CITATION.cff`. These references use the GitHub URL;\n"
            "they will include the DOI after the metadata are updated.\n\n"
        )
    block += (
        "For reproducible research, cite the version you used and its version DOI when available.\n"
        "[First release and Zenodo instructions](docs/releases.md).\n"
        f"{END}"
    )
    readme = root / "README.md"
    content = readme.read_text()
    if content.count(START) != 1 or content.count(END) != 1:
        raise ValueError("README.md must contain exactly one citation marker pair")
    before, remaining = content.split(START, 1)
    _, after = remaining.split(END, 1)
    outputs = {
        root / "CITATION.txt": text,
        root / "CITATION.bib": bibtex,
        readme: before + block + after,
    }
    stale = [path.name for path, value in outputs.items() if not path.exists() or path.read_text() != value]
    if check and stale:
        raise ValueError("Regenerate citations with python .github/scripts/citation.py: " + ", ".join(stale))
    if not check:
        for path, value in outputs.items():
            path.write_text(value)
    print("Citation files and README are current." if check else "Updated citation files and README.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check without changing files")
    args = parser.parse_args()
    try:
        update(Path(__file__).resolve().parents[2], args.check)
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{error}\n")
