"""Keep repository Markdown usable both on GitHub and in the rendered book."""

import json
import posixpath
import re
import sys
from pathlib import Path, PurePosixPath
from urllib.parse import quote, unquote, urlsplit, urlunsplit


def chapters(items):
    for item in items:
        if "Chapter" in item:
            chapter = item["Chapter"]
            yield chapter
            yield from chapters(chapter.get("sub_items", []))


def preprocess(context, book):
    root = Path(context["root"])
    repository = context["config"]["output"]["html"]["git-repository-url"].rstrip("/")
    source = PurePosixPath(context["config"]["book"]["src"])
    items = list(chapters(book["items"]))
    targets = {
        str(source / chapter["source_path"]): chapter["path"]
        for chapter in items
        if chapter.get("source_path") and chapter.get("path")
    }
    targets.update({"README.md": "index.md", "examples/README.md": "examples.md"})

    for chapter in items:
        if not chapter.get("source_path") or not chapter.get("path"):
            continue
        origin = source / chapter["source_path"]
        # The landing chapter includes the repository README verbatim.
        if chapter["source_path"] == "index.md":
            origin = PurePosixPath("README.md")

        def link(match):
            target = match[2]
            url = urlsplit(target)
            if url.scheme or url.netloc or not url.path:
                return match[0]
            resolved = posixpath.normpath(str(origin.parent / unquote(url.path)))
            if resolved in targets:
                path = posixpath.relpath(targets[resolved], str(PurePosixPath(chapter["path"]).parent))
                href = urlunsplit(("", "", path, url.query, url.fragment))
            elif not resolved.startswith("../") and (root / resolved).is_file():
                href = f"{repository}/blob/HEAD/{quote(resolved, safe='/')}"
                if url.query:
                    href += "?" + url.query
                if url.fragment:
                    href += "#" + url.fragment
            else:
                raise ValueError(f"Broken documentation link in {origin}: {target}")
            return match[1] + href + match[3]

        # Preserve literal links inside examples and fenced source code.
        lines = []
        fence = None
        for line in chapter["content"].splitlines(keepends=True):
            marker = re.match(r"^\s*(`{3,}|~{3,})", line)
            if marker:
                if fence is None:
                    fence = marker[1][0]
                elif marker[1][0] == fence:
                    fence = None
                lines.append(line)
            elif fence:
                lines.append(line)
            else:
                lines.append(re.sub(r"(\]\()([^\s)]+)(\))", link, line))
        chapter["content"] = "".join(lines)
    return book


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "supports":
        sys.exit(0 if sys.argv[2:] == ["html"] else 1)
    context, book = json.load(sys.stdin)
    json.dump(preprocess(context, book), sys.stdout)
