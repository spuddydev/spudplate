# spudplate

[![CI](https://github.com/spuddydev/spudplate/actions/workflows/ci.yml/badge.svg)](https://github.com/spuddydev/spudplate/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

**Templating made for spuds!**

Write a `.spud` file in spudlang, install it once, and run it whenever you want to scaffold a new project. The interpreter prompts the user and creates files and directories based on their answers.

> **Pre-1.0.** spudplate works end to end but breaking changes are expected before `v1.0`. Pin to a specific release if you need stability.

## Install

One-liner (Linux x86_64, macOS arm64):

```
curl -fsSL https://raw.githubusercontent.com/spuddydev/spudplate/main/install.sh | sh
```

Drops the binary in `~/.local/bin`. Set `PREFIX=/usr/local` (and run with `sudo`) to install system-wide, or `VERSION=v0.1.0` to pin to a tag.

Alternatives:
- Download a binary from the [releases page](https://github.com/spuddydev/spudplate/releases) and put it on your `PATH`. `SHA256SUMS` is published alongside.
- Build from source - see [Build](#build) below, then `sudo cmake --install build`.

## How it works

1. Write a `.spud` file describing your template
2. Install it with `spudplate install my_template.spud` - spudplate bundles every asset the template references and stores everything as a single `<name>.spp` file under the install root
3. Run it any time with `spudplate run my_template` - runs work from any working directory because the assets travel with the template

```
spudplate install my_template.spud      # bundle assets, write <name>.spp under the install root
spudplate install --yes my_template.spud  # overwrite an existing install without prompting
spudplate validate my_template.spud     # parse and validate without installing (also: `check`)
spudplate run my_template               # run by installed name
spudplate run path/to/file.spud         # or run a file directly (cwd-relative assets)
spudplate run path/to/file.spp          # or run a built spudpack directly
spudplate list                          # list installed templates
spudplate inspect my_template           # print the source captured at install time
spudplate uninstall my_template         # remove

spudplate version                       # print the spudplate version
spudplate update                        # fetch and install the latest spudplate release
```

`install` prompts before overwriting an existing template. Pass `--yes` to skip the prompt (useful for scripts and CI). `update` fetches the latest release of spudplate itself by re-running the install script.

To share a template, send the `<name>.spp` file. The recipient runs it with `spudplate run path/to/template.spp`. (Direct `install` from a `.spp` is intentionally not supported in this version - share the source instead, or run the spudpack directly.)

The install root is `$SPUDPLATE_HOME` if set, otherwise `$XDG_DATA_HOME/spudplate` (default `~/.local/share/spudplate` on most systems).

## Example

```
ask project_name "Project name?" string
ask use_tests "Include a test suite?" bool default false
ask license "License?" string default "MIT"

let slug = lower(trim(project_name))

mkdir {slug} as project
mkdir project/src
mkdir project/tests when use_tests

file project/README.md content "# " + project_name
file project/src/main.cpp from templates/main.cpp
file project/tests/test_main.cpp from templates/test_main.cpp when use_tests
```

Spudlang supports questions with defaults and option lists, derived variables, conditional actions, path aliases, loops, and including other installed templates. See the [language reference](https://spuddydev.github.io/spudplate/) for the full syntax.

## Editor support

Syntax highlighting for `.spud` files is available through the [spudlang-vscode](https://github.com/spuddydev/spudlang-vscode) extension.

## Build

Requires CMake and a C++20 compiler.

```bash
cmake -B build && cmake --build build
```

## Test

```bash
ctest --test-dir build
```

## Docs

Requires [Doxygen](https://www.doxygen.nl/) (`brew install doxygen`).

```bash
cmake --build build --target docs
```

Opens `docs/html/index.html` - includes the full spudlang syntax reference and API documentation. Published online at https://spuddydev.github.io/spudplate/.

## Contributing

Bug reports, feature requests, and pull requests are welcome. See [CONTRIBUTING](CONTRIBUTING.md) for build and submission guidelines, and the [Code of Conduct](CODE_OF_CONDUCT.md).

## License

MIT - see [LICENSE](LICENSE).
