# spudplate

Imagine you're spinning up a new project from your team's standard layout, where the choice of test framework, license, and CI matrix changes every time. Cookiecutter would have you template the easy bits in Jinja and write a post-generation hook for the conditionals. Spudplate lets you put it all in one `.spud` file, every new project is `spudplate run team-default` and a couple of prompts.

Write a `.spud` file in spudlang, install it once, and run it whenever you want to scaffold a new project. The interpreter prompts the user and creates files and directories based on their answers.

## Introduction

### Documentation

| Page | What's in it |
|------|--------------|
| [Getting Started](getting-started.md) | Install spudplate, write a tiny `.spud`, install and run it. |
| [Spudlang Syntax Reference](syntax.md) | The full language: questions, actions, expressions, scoping rules. |
| [CLI Reference](cli.md) | Every subcommand, every flag, every environment variable. |
| [Spudpack Binary Format](spudpack-format.md) | The on-disk layout of `.spp` files. |
| [API Reference](annotated.html) | C++ class and struct documentation generated from the headers. |

### Project links

- **Source code:** <https://github.com/spuddydev/spudplate>
- **Releases:** <https://github.com/spuddydev/spudplate/releases>
- **Issue tracker:** <https://github.com/spuddydev/spudplate/issues>
- **Editor support:** [spudlang-vscode](https://github.com/spuddydev/spudlang-vscode) for `.spud` syntax highlighting

### A small example

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

Read the [syntax reference](syntax.md) for the rest.

---

**Pre-1.0.** Spudplate works end to end but breaking changes are expected before `v1.0`. Pin to a specific release if you need stability.
