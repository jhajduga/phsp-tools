# Documentation and GitHub Pages

[Back to README](../README.md)

The English documentation is also published as an mdBook. It provides full-text
search, chapter navigation, light/dark themes, native admonitions and Mermaid diagrams.
The content comes from the existing Markdown files in `docs/`; the landing page
includes the repository README and the preparation example includes its actual JSON.
Changes to those files therefore update both GitHub documentation and the book.

## Build locally

On Linux x86_64, install the pinned release binaries and Mermaid assets:

```sh
bash .github/scripts/install-mdbook.sh
export PATH="$PWD/.cache/mdbook/bin:$PATH"
mdbook build .
python3 .github/scripts/check-book-links.py book
```

Open `book/index.html`, or serve it with live reload:

```sh
mdbook serve . --open
```

The installer downloads **mdBook 0.5.4** and **mdbook-mermaid 0.17.1** from their
official releases and verifies pinned SHA-256 checksums. It requires Bash, curl,
tar and sha256sum. No Rust compilation or ROOT installation is needed for the book.
For other platforms install those same versions from their upstream releases or Cargo,
then run `mdbook-mermaid install .` before building. Mermaid is the only required
plugin; mdBook itself supplies admonitions, search and chapter navigation.

Generated HTML, downloaded tools and Mermaid assets are ignored by Git. The build
does not read or publish PHSP input files, analysis results or CMake build trees.

## GitHub Pages setup

The workflow is [`.github/workflows/mdbook.yml`](../.github/workflows/mdbook.yml).
For the repository that will host the site:

1. Open **Settings → Pages → Build and deployment**.
2. Set **Source** to **GitHub Actions**.
3. Push the workflow and documentation to **main**, or run the documentation workflow
   manually with main selected.
4. Open the URL reported by the `github-pages` deployment.

These are the [GitHub instructions for custom Pages workflows](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages).
Pages enablement is a repository setting; adding a workflow alone does not change it.

The workflow:

- Builds and checks local links/anchors for pull requests, without deployment.
- Builds, uploads the Pages artifact and deploys pushes to main.
- Supports manual runs; deployment is restricted to main.
- Derives the repository URL and Pages base path from GitHub metadata, so a repository
  rename or custom domain does not require hardcoding a new path in the workflow.
- Gives the deployment job `pages: write` and `id-token: write`; no personal token is needed.

If deployment should move to another branch, update both the push branch and the
main-branch conditions in `mdbook.yml`. Keep builds for pull requests enabled.

## Editing the book

`book.toml` defines the renderer and preprocessors; `docs/SUMMARY.md` defines chapter
order. Add new Markdown chapters to the summary. Keep normal relative links in the
source files so they continue to work on GitHub.

The small [link preprocessor](../.github/scripts/mdbook-links.py) maps repository
README links to the book landing page, chapter links to their rendered locations,
and source/example/licence links to GitHub. It preserves literal code examples and
fails on missing source destinations. The [HTML checker](../.github/scripts/check-book-links.py)
checks the generated assets, pages and fragment targets before upload.

For a build served under an explicit project path, for example `/phsp-tools/`:

```sh
MDBOOK_OUTPUT__HTML__SITE_URL=/phsp-tools/ mdbook build .
python3 .github/scripts/check-book-links.py book --base-path /phsp-tools/
```

The official [mdBook CI guide](https://rust-lang.github.io/mdBook/continuous-integration.html)
describes release-binary installation and the `site-url` setting used for subdirectory hosting.
