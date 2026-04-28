# spudplate

**Templating made for spuds.**

Spinning up a new project always devolves into the same routine: copy an old folder, sed-replace the names, rewrite the README, wire up CI from memory. Cookiecutter and Yeoman handle the mechanical parts well, but the conditional bits - the parts that actually vary between projects - get punted to Jinja-in-filenames and post-generation hook scripts. Spudplate makes the template a real program. `if`, `when`, `repeat`, derived variables, and shell commands all live inside it, and the whole thing ships as a single `.spp` file with no Python or Node runtime to install on the recipient's machine.

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

> **Pre-1.0.** Spudplate works end to end but breaking changes are expected before `v1.0`. Pin to a specific release if you need stability.
