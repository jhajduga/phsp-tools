"""Validate a committed release and create source archives locally; never publish."""

import argparse
import hashlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

import yaml


def prepare(root, version, output, ref):
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise ValueError("Use a version in X.Y.Z format, without a leading v")

    def git(*args):
        return subprocess.check_output(["git", *args], cwd=root, text=True).strip()

    commit = git("rev-parse", "--verify", ref + "^{commit}")
    if commit != git("rev-parse", "HEAD"):
        raise ValueError("Check out the requested commit before preparing its release")
    if git("status", "--porcelain", "--untracked-files=normal"):
        raise ValueError("Commit all intended release changes before preparing source archives")
    metadata = yaml.safe_load((root / "CITATION.cff").read_text())
    if str(metadata["version"]) != version:
        raise ValueError("Requested version does not match CITATION.cff; update version metadata first")
    subprocess.run([sys.executable, str(root / ".github/scripts/citation.py"), "--check"], check=True)
    notes = root / "docs/release_notes" / (version + ".md")
    if not notes.is_file():
        raise ValueError(f"Write release notes in docs/release_notes/{version}.md first")
    tag = "v" + version
    existing = subprocess.run(
        ["git", "rev-parse", "--verify", "--quiet", f"refs/tags/{tag}^{{commit}}"],
        cwd=root, text=True, capture_output=True
    )
    if existing.returncode == 0 and existing.stdout.strip() != commit:
        raise ValueError(f"Tag {tag} already points to a different commit; do not move a released tag")
    output = output if output.is_absolute() else root / output
    if output.exists() and any(output.iterdir()):
        raise ValueError("Output directory is not empty; choose a fresh output directory")
    output.mkdir(parents=True, exist_ok=True)
    stem = "phsp-tools-" + version
    for format, extension in [("tar.gz", "tar.gz"), ("zip", "zip")]:
        subprocess.run(
            ["git", "archive", "--format=" + format, "--prefix=" + stem + "/",
             "--output=" + str(output / (stem + "." + extension)), commit],
            cwd=root, check=True
        )
    for name in ["CITATION.cff", "CITATION.bib", "CITATION.txt", "LICENSE"]:
        shutil.copyfile(root / name, output / name)
    (output / "RELEASE_NOTES.md").write_text(
        notes.read_text().rstrip() + f"\n\nSource commit: `{commit}`.\n"
    )
    checksums = []
    for path in sorted(output.iterdir()):
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        checksums.append(f"{digest}  {path.name}\n")
    (output / "SHA256SUMS").write_text("".join(checksums))
    print(f"Prepared {tag} from {commit} in {output}; nothing has been published.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, default=Path(".cache/release"))
    parser.add_argument("--ref", default="HEAD")
    args = parser.parse_args()
    try:
        prepare(Path(__file__).resolve().parents[2], args.version, args.output, args.ref)
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{error}\n")
