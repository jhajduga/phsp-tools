# Releases, citation and Zenodo

[Back to README](../README.md)

A commit records a source change. A **tag**, such as `v0.3.0`, names a specific commit.
A GitHub **release** adds a description and downloadable files to that tag. A **draft**
is an unpublished release you can review before making it public.

The current project version is **0.3.0**, so use **v0.3.0** for the first release.
A first public release does not have to be called 1.0.0.

## First release: recommended route

### 1. Connect Zenodo before publishing

1. Sign in to [Zenodo](https://zenodo.org/) using your GitHub account and authorize its integration.
2. Open your profile menu → **GitHub**, then click **Sync now** if necessary.
3. Enable **jhajduga/phsp-tools** in the repository list.

Zenodo's [repository connection guide](https://help.zenodo.org/docs/github/enable-repository/)
describes these settings. The integration archives new published releases and provides
their DOIs; creating a local tag or a GitHub draft is not the publication step.
[Zenodo release archiving](https://help.zenodo.org/docs/github/archive-software/github-upload/).

This workflow uses the integration, so it does not require a Zenodo API token in
GitHub Actions. A separate manual-upload route is also available on Zenodo if you
prefer to control the deposit there yourself.

### 2. Commit and push the release preparation

Before releasing, commit the intended code, documentation and workflows to **main**.
Check that:

- `CMakeLists.txt` and `CITATION.cff` both say `0.3.0`.
- [Release notes](release_notes/0.3.0.md) describe the version you intend to publish.
- CI and the documentation build pass.

Generated references come from `CITATION.cff`. If you edit its author, version, URL,
release date or DOI, regenerate the text/BibTeX files and README block:

```sh
python3 -m venv .cache/release-tools
.cache/release-tools/bin/python -m pip install -r .github/requirements-citation.txt
.cache/release-tools/bin/python .github/scripts/citation.py
```

Commit `CITATION.cff`, `CITATION.bib`, `CITATION.txt` and README together. The normal CI
workflow checks that the references and CMake version agree with the citation metadata.
An unset publication date is left unset; no DOI or release date is invented.

### 3. Prepare the draft from Actions

1. Open **Actions → Prepare draft release → Run workflow**.
2. Select branch **main** and version **0.3.0**, without the leading `v`.
3. Wait for tests and citation checks to pass.
4. Follow the draft-release URL in the workflow summary, or open **Releases**.

[release.yml](../.github/workflows/release.yml) runs only when requested. It reuses
the normal build/test workflow and then creates an **unpublished draft**. It pins the
release to the commit selected by the workflow run, even if main changes afterwards.
A tag named `v0.3.0` is associated with the draft; you do not have to create it by hand.
The preparation does not replace an existing release or move a tag to another commit.

The draft includes:

- `phsp-tools-0.3.0.tar.gz` and `.zip`, containing the committed source tree;
- `CITATION.cff`, `CITATION.bib`, `CITATION.txt` and `LICENSE`;
- `RELEASE_NOTES.md`, including the source commit;
- `SHA256SUMS` for the attached files.

These are **source distributions**, requiring ROOT and the other documented build
dependencies. They are not self-contained binaries. Generated PHSP/results/build
files ignored by Git are not included by `git archive`.

### 4. Review, then publish

Open the draft and review its version, tag, release notes and assets. When ready,
click **Publish release** in GitHub. The workflow deliberately leaves this last step
to you. See [GitHub's release guide](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository)
and the [draft behaviour of GitHub CLI](https://cli.github.com/manual/gh_release_create).

The release does not exist as a public published version until this step. Zenodo
then processes the release asynchronously. Check the repository's entry in your
Zenodo GitHub page for completion or metadata errors.

### 5. Add the DOI after Zenodo assigns it

Open the resulting Zenodo record. There are two relevant identifiers:

- **Version DOI**: the exact archived software version, appropriate for reproducible research.
- **Concept DOI**: the software across its versions, useful as a general project reference.

[Zenodo DOI versioning](https://support.zenodo.org/help/en-gb/1-upload-deposit/97-what-is-doi-versioning)
explains the distinction. For the version currently described by `CITATION.cff`, add
the actual version DOI as its top-level `doi` value and the actual publication date
as `date-released` in YYYY-MM-DD format. Do not paste a DOI for a different version.

Run the citation generator again and commit the updated metadata/references on main.
This updates the copyable README citation and downloadable files. You can also add
the Zenodo link to the published GitHub release description. The release's existing
source archive remains the original snapshot: do not move its tag merely to insert
a DOI that was assigned afterwards.

The GitHub integration cannot reserve a DOI before a release. If a DOI must appear
inside the very first archived source snapshot, use Zenodo's manual-upload route and
reserve it there instead. [Zenodo's explanation](https://support.zenodo.org/help/en-gb/24-github-integration/73-can-i-pre-reserved-a-doi-before-a-github-release).

## Citation downloads and copying

The end of the [README](../README.md) provides downloadable BibTeX/plain text and
copyable versions of both. GitHub's **Cite this repository** panel also reads
`CITATION.cff` and offers APA/BibTeX. [GitHub citation documentation](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-citation-files).

The generator validates the CFF schema using `cffconvert`. For classic BibTeX styles,
it retains the software version in a `note` field as well as the `version` field.
No `.zenodo.json` is needed for this workflow: Zenodo reads `CITATION.cff`. If you later
add `.zenodo.json`, it takes precedence over the CFF metadata in Zenodo.
[Zenodo metadata rules](https://help.zenodo.org/docs/github/describe-software/citation-file/).

## Later releases

For a new version such as 0.3.1:

1. Update the CMake and CFF versions together. Remove the previous version's `doi`
   and `date-released` until the new values are known.
2. Write `docs/release_notes/0.3.1.md` describing the actual changes.
3. Regenerate citation files, commit and push to main.
4. Run **Prepare draft release** with version 0.3.1; review and publish the draft.
5. Add the new version DOI after Zenodo finishes archiving it.

GitHub archives are snapshots of a tag. Treat an already published tag as fixed;
release a new version to distribute changed source code.

## Local preparation without a GitHub release

After committing the intended changes, you can inspect the same assets locally:

```sh
.cache/release-tools/bin/python .github/scripts/prepare-release.py \
  --version 0.3.0 --output .cache/release-preview
```

This command only creates local files. It does not create a tag, contact GitHub or
publish anything. It requires a clean Git working tree and a fresh output directory,
and rejects version mismatches or an existing tag pointing to another commit.

The workflow's final `gh release create --draft` step requires `contents: write` for
its automatic `GITHUB_TOKEN`. It does not need a personal access token or Zenodo secret.
