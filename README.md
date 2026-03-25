# spudplate

[![CI](https://github.com/spuddydev/spudplate/actions/workflows/ci.yml/badge.svg)](https://github.com/spuddydev/spudplate/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

**Templating made for spuds!**

Write a `.spud` file in spudlang, compile it into a standalone binary that asks the user questions and creates files and directories based on their answers.

## How it works

1. Write a `.spud` file describing your template
2. Compile it with `spudplate` into a standalone binary
3. Distribute or run the binary, it prompts the user and scaffolds the project!

```
spudplate my_template.spud -o my_template
./my_template
```

## Example

```
ask project_name "Project name?" string required
ask use_tests "Include a test suite?" bool

let slug = lower(trim(project_name))

mkdir "{slug}"
mkdir "{slug}/src"
mkdir "{slug}/tests" when use_tests

file "{slug}/README.md" content "# " + project_name
file "{slug}/src/main.cpp" from "templates/main.cpp"
file "{slug}/tests/test_main.cpp" from "templates/test_main.cpp" when use_tests
```

Spudlang supports variables, string/int expressions, conditionals (`when`), and `repeat` loops. See [docs/syntax.md](docs/syntax.md) for the full language reference.

## Build

> **Note:** spudplate is currently in active development. The lexer and parser are complete, but the compiler is not yet functional. Compiled binaries cannot be produced yet.

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

Opens `docs/html/index.html` — includes the full spudlang syntax reference and API documentation.

## License

MIT — see [LICENSE](LICENSE).
