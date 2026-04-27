# Contributing to spudplate

Thanks for your interest in spudplate. This guide covers how to build, test, and submit changes.

## Code of conduct

By participating, you agree to abide by the [Code of Conduct](CODE_OF_CONDUCT.md).

## Getting started

Requires CMake and a C++20 compiler. `clang-format` and `clang-tidy` are used for formatting and linting.

```bash
cmake -B build && cmake --build build        # build
ctest --test-dir build                        # run tests
clang-format -i src/**/*.cpp include/**/*.h   # format
clang-tidy src/**/*.cpp -- -Iinclude          # lint
```

Doxygen docs build with `cmake --build build --target docs` and land in `docs/html/index.html`.

## Branching

- Branch off the latest merged work on `main`.
- Keep branches small and focused on a single feature, fix, or refactor. Do not bundle unrelated changes.

## Commits

- Conventional commits style: `feat:`, `fix:`, `test:`, `refactor:`, `chore:`, `docs:`, `ci:`.
- One small focused change per commit. Split implementation and tests into separate commits.
- Plain words, British English spelling (organise, colour, behaviour).
- Use snake_case for local and member variables.

## Tests

Write tests for every major feature. Tests live under `tests/` and run via `ctest`.

## Pull requests

The repo ships a PR template - fill it in. Keep summaries short, use bullets, and describe the change on its own terms.

A PR is ready when:

- It builds cleanly and `ctest` passes locally.
- Code is formatted (`clang-format`) and lint-clean (`clang-tidy`).
- New behaviour is covered by tests.
- The summary explains what changed and how to verify it.

## Reporting issues

Use the issue templates for bug reports and feature requests. Include enough detail that a maintainer can reproduce or evaluate the request without follow-up.

## Editor support

A syntax highlighting extension for `.spud` files is available at [spuddydev/spudlang-vscode](https://github.com/spuddydev/spudlang-vscode).
