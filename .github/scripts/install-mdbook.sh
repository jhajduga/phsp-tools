#!/usr/bin/env bash
set -euo pipefail

# Pinned upstream release binaries for the Linux x86_64 Pages runner.
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
binary_dir="${1:-${repo_root}/.cache/mdbook/bin}"
mkdir -p "$binary_dir"
binary_dir="$(cd -- "$binary_dir" && pwd)"
archive="$(mktemp)"
trap 'rm -f "$archive"' EXIT

install_binary() {
    local repository="$1" version="$2" name="$3" checksum="$4"
    curl --fail --silent --show-error --location --retry 3 \
        "https://github.com/${repository}/releases/download/v${version}/${name}-v${version}-x86_64-unknown-linux-gnu.tar.gz" \
        --output "$archive"
    printf '%s  %s\n' "$checksum" "$archive" | sha256sum --check --status
    tar -xzf "$archive" -C "$binary_dir" "$name"
}

install_binary rust-lang/mdBook 0.5.4 mdbook \
    3f28de05dafca9d0f2eab99c662116b0e37b89b1d96a08f8f430b9eeae958cd7
install_binary badboy/mdbook-mermaid 0.17.1 mdbook-mermaid \
    9afcfa5b8463afe606d48595a7ae338564302903e626ea5b6edb8007d29393a5

# Copies Mermaid's assets; the matching configuration is already in book.toml.
(cd "$repo_root" && "$binary_dir/mdbook-mermaid" install .)
"$binary_dir/mdbook" --version
"$binary_dir/mdbook-mermaid" --version
printf 'Installed documentation tools in %s\n' "$binary_dir"
