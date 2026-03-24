# spudplate

A template scaffolding compiler. Write a `.spud` file, compile it into a standalone binary that interactively creates files and directories.

## Build

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
