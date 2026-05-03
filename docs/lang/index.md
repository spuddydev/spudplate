# Spudlang Language Reference {#lang_reference}

Spudlang is the language used to write `.spud` files. It is a small, line-oriented imperative language designed for one job: ask a series of questions and create files based on the answers.

A `.spud` file executes top-to-bottom. Questions and actions can be freely interleaved, and filesystem writes are deferred to the end of the run, so a partial run never leaves a half-written project on disk.

```
ask project_name "Project name?" string
ask use_tests "Include a test suite?" bool default false

let slug = lower(trim(project_name))

mkdir "{slug}" as project
mkdir project/"src"
mkdir project/"tests" when use_tests

file project/"README.md" content "# " + project_name
file project/"src/main.cpp" from "templates/main.cpp"
```

This reference describes the language as it stands today. The [CLI reference](../cli.md) covers the commands that operate on `.spud` and `.spp` files. The [getting started guide](../getting-started.md) walks through writing and running a first template.

## Reference

- @subpage lang_lexical (comments, line continuation, identifiers, keywords, literals)
- @subpage lang_types (types, operators, function calls, and `{expr}` interpolation)
- @subpage lang_paths (path expressions, aliases, and the rules for using each)
- @subpage lang_statements (every statement: `ask`, `let`, `mkdir`, `file`, `copy`, `repeat`, `if`, `include`, `run`)
- @subpage lang_scoping (variable scoping, `repeat` and `if` scope, alias scoping, and condition normalisation)

## Guides

- @subpage lang_best_practices (how to write idiomatic, maintainable templates)
- @subpage lang_pitfalls (known footguns and how to avoid them)
- @subpage lang_examples (fully-worked examples from minimal to full-featured)

## Conventions used in this reference

Code samples are valid spudlang and assume any referenced asset (a `from` source path, an installed template named on `include`) is reachable when the template runs. Where a feature has known limitations, the reference describes what the language does today rather than what is planned.
