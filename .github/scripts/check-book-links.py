"""Check local HTML destinations and anchors in an already rendered mdBook."""

import argparse
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit


class Page(HTMLParser):
    def __init__(self, text):
        super().__init__(convert_charrefs=True)
        self.ids = set()
        self.links = []
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        for name, value in attrs:
            if name == "id" or (tag == "a" and name == "name"):
                self.ids.add(value)
            if name in {"href", "src"} and value:
                self.links.append(value)


def check(root, base_path):
    root = root.resolve()
    pages = {p.resolve(): Page(p.read_text()) for p in root.rglob("*.html")}
    if root / "index.html" not in pages:
        raise ValueError("No generated index.html; build the book first")
    errors = []
    prefix = "/" + base_path.strip("/") + "/" if base_path.strip("/") else "/"
    for path, page in pages.items():
        for link in page.links:
            url = urlsplit(link)
            if url.scheme or url.netloc:
                continue
            relative = unquote(url.path)
            if relative.startswith("/"):
                if not relative.startswith(prefix):
                    errors.append(f"{path.relative_to(root)}: outside site base: {link}")
                    continue
                target = root / relative[len(prefix) :]
            else:
                target = path.parent / relative if relative else path
            target = target.resolve()
            if target.is_dir():
                target /= "index.html"
            if not target.is_relative_to(root) or not target.is_file():
                errors.append(f"{path.relative_to(root)}: missing destination: {link}")
                continue
            fragment = unquote(url.fragment)
            if fragment and fragment != "top" and target in pages and fragment not in pages[target].ids:
                errors.append(f"{path.relative_to(root)}: missing anchor: {link}")
    if errors:
        raise ValueError("\n".join(errors))
    print(f"Checked {len(pages)} HTML pages: no broken local links or fragments.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--base-path", default="")
    args = parser.parse_args()
    check(args.directory, args.base_path)
