#!/bin/sh
# spudplate installer — fetches the release binary for your OS/arch from
# GitHub Releases, verifies its checksum, and drops it on your PATH.
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/spuddydev/spudplate/main/install.sh | sh
#
# Environment overrides:
#   PREFIX   install root (default $HOME/.local). Binary lands in $PREFIX/bin.
#   VERSION  release tag to install (default: latest).
#
# Pre-1.0 software — breaking changes are expected before v1.0.

set -eu

REPO="spuddydev/spudplate"
PREFIX="${PREFIX:-$HOME/.local}"
VERSION="${VERSION:-latest}"

err() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

info() {
    printf '%s\n' "$*"
}

detect_target() {
    os=$(uname -s)
    arch=$(uname -m)
    case "$os" in
        Linux)  os_tag=linux ;;
        Darwin) os_tag=darwin ;;
        *)      err "unsupported OS: $os" ;;
    esac
    case "$arch" in
        x86_64|amd64)  arch_tag=x86_64 ;;
        arm64|aarch64) arch_tag=arm64 ;;
        *)             err "unsupported architecture: $arch" ;;
    esac
    if [ "$os_tag" = linux ] && [ "$arch_tag" != x86_64 ]; then
        err "no Linux $arch_tag build yet — build from source: https://github.com/$REPO"
    fi
    if [ "$os_tag" = darwin ] && [ "$arch_tag" != arm64 ]; then
        err "no macOS $arch_tag build yet — build from source: https://github.com/$REPO"
    fi
    target="spudplate-${os_tag}-${arch_tag}"
}

resolve_urls() {
    if [ "$VERSION" = latest ]; then
        base="https://github.com/$REPO/releases/latest/download"
    else
        base="https://github.com/$REPO/releases/download/$VERSION"
    fi
    binary_url="$base/$target"
    sums_url="$base/SHA256SUMS"
}

download() {
    url="$1"
    out="$2"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL -o "$out" "$url"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "$out" "$url"
    else
        err "neither curl nor wget is installed"
    fi
}

verify_checksum() {
    file="$1"
    sums="$2"
    expected=$(awk -v t="$target" '$2 == t || $2 == "*"t { print $1; exit }' "$sums")
    if [ -z "$expected" ]; then
        err "no checksum entry for $target in SHA256SUMS"
    fi
    if command -v shasum >/dev/null 2>&1; then
        actual=$(shasum -a 256 "$file" | awk '{ print $1 }')
    elif command -v sha256sum >/dev/null 2>&1; then
        actual=$(sha256sum "$file" | awk '{ print $1 }')
    else
        err "no shasum or sha256sum available to verify the download"
    fi
    if [ "$expected" != "$actual" ]; then
        err "checksum mismatch: expected $expected, got $actual"
    fi
}

main() {
    detect_target
    resolve_urls
    info "spudplate installer"
    info "  target:  $target"
    info "  version: $VERSION"
    info "  prefix:  $PREFIX"

    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT INT TERM

    info "downloading $binary_url"
    download "$binary_url" "$tmp/$target"
    info "downloading checksums"
    download "$sums_url" "$tmp/SHA256SUMS"

    verify_checksum "$tmp/$target" "$tmp/SHA256SUMS"

    mkdir -p "$PREFIX/bin"
    install_path="$PREFIX/bin/spudplate"
    mv "$tmp/$target" "$install_path"
    chmod +x "$install_path"

    info ""
    info "installed to $install_path"

    case ":$PATH:" in
        *":$PREFIX/bin:"*) ;;
        *)
            info ""
            info "warning: $PREFIX/bin is not on your PATH"
            info "add this line to your shell config (~/.bashrc, ~/.zshrc, etc.):"
            info "  export PATH=\"$PREFIX/bin:\$PATH\""
            ;;
    esac
}

main "$@"
