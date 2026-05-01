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

if command -v curl >/dev/null 2>&1; then
    fetch() { curl -fsSL -o "$1" "$2"; }
elif command -v wget >/dev/null 2>&1; then
    fetch() { wget -qO "$1" "$2"; }
else
    err "neither curl nor wget is installed"
fi

if command -v sha256sum >/dev/null 2>&1; then
    sha256() { sha256sum "$1" | awk '{ print $1 }'; }
elif command -v shasum >/dev/null 2>&1; then
    sha256() { shasum -a 256 "$1" | awk '{ print $1 }'; }
else
    err "no shasum or sha256sum available to verify the download"
fi

main() {
    os=$(uname -s)
    arch=$(uname -m)
    case "$os $arch" in
        "Linux x86_64"|"Linux amd64")    target=spudplate-linux-x86_64 ;;
        "Darwin arm64"|"Darwin aarch64") target=spudplate-darwin-arm64 ;;
        "Linux "*)  err "no Linux $arch build yet — build from source: https://github.com/$REPO" ;;
        "Darwin "*) err "no macOS $arch build yet — build from source: https://github.com/$REPO" ;;
        *)          err "unsupported OS: $os" ;;
    esac

    if [ "$VERSION" = latest ]; then
        base="https://github.com/$REPO/releases/latest/download"
    else
        base="https://github.com/$REPO/releases/download/$VERSION"
    fi

    info "spudplate installer"
    info "  target:  $target"
    info "  version: $VERSION"
    info "  prefix:  $PREFIX"

    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT INT TERM

    info "downloading $base/$target"
    fetch "$tmp/$target" "$base/$target"
    info "downloading checksums"
    fetch "$tmp/SHA256SUMS" "$base/SHA256SUMS"

    expected=$(awk -v t="$target" '$2 == t || $2 == "*"t { print $1; exit }' "$tmp/SHA256SUMS")
    [ -n "$expected" ] || err "no checksum entry for $target in SHA256SUMS"
    actual=$(sha256 "$tmp/$target")
    [ "$expected" = "$actual" ] || err "checksum mismatch: expected $expected, got $actual"

    mkdir -p "$PREFIX/bin"
    install_path="$PREFIX/bin/spudplate"
    mv "$tmp/$target" "$install_path"
    chmod +x "$install_path"

    info ""
    info "installed to $install_path"

    install_completions "$install_path"

    case ":$PATH:" in
        *":$PREFIX/bin:"*) ;;
        *)
            info ""
            info "warning: $PREFIX/bin is not on your PATH"
            info "add this line to your shell config (~/.bashrc, ~/.zshrc, etc.):"
            info "  export PATH=\"$PREFIX/bin:\$PATH\""
            ;;
    esac

    info ""
    info "to uninstall later: spudplate self-uninstall (add --purge to also drop installed templates)"
}

install_completions() {
    bin="$1"
    bash_dir="$HOME/.local/share/bash-completion/completions"
    zsh_dir="$HOME/.zsh/completions"
    if ! mkdir -p "$bash_dir" "$zsh_dir" 2>/dev/null; then
        info "note: could not create completion directories; skipping completion install"
        return 0
    fi
    if ! "$bin" completion bash >"$bash_dir/spudplate" 2>/dev/null; then
        info "note: could not install bash completion"
        return 0
    fi
    if ! "$bin" completion zsh >"$zsh_dir/_spudplate" 2>/dev/null; then
        info "note: could not install zsh completion"
        return 0
    fi
    info ""
    info "shell completion installed:"
    info "  bash: $bash_dir/spudplate"
    info "  zsh:  $zsh_dir/_spudplate"

    setup_zshrc
}

setup_zshrc() {
    [ "$(basename "${SHELL:-}")" = zsh ] || return 0
    zshrc="$HOME/.zshrc"
    marker_start="# >>> spudplate completion >>>"
    marker_end="# <<< spudplate completion <<<"
    if [ -f "$zshrc" ] && grep -qF "$marker_start" "$zshrc"; then
        return 0
    fi
    {
        printf '\n%s\n' "$marker_start"
        # shellcheck disable=SC2016  # written verbatim into .zshrc; zsh expands $fpath at startup
        printf '%s\n' 'fpath=(~/.zsh/completions $fpath)'
        printf '%s\n' 'autoload -U compinit && compinit'
        printf '%s\n' "$marker_end"
    } >>"$zshrc"
    info ""
    info "added zsh completion setup to $zshrc"
    info "open a new shell or run 'source $zshrc' to enable"
}

main "$@"
