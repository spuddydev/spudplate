# Examples {#lang_examples}

Three fully-worked templates, building up from minimal to realistic. Every example here is valid spudlang and parses cleanly. Each is annotated with the patterns it demonstrates.

[TOC]

## Minimal: a single-file greeter

The smallest useful template. One question, one directory, one file.

```
ask name "Your name?" string

mkdir "hello" as dir
file dir/"greeting.txt" content "Hello, {name}.
Glad to meet you.
"
```

What this shows:

- A required `ask` (no `default`).
- A `mkdir` with an `as` alias for reuse.
- `{name}` interpolation inside a string literal in `content`.
- A real newline embedded inside the string literal (the line break in the source becomes a line break in the output).

A run looks like this:

```
$ spudplate run hello
[1/1] Your name?
> Joe
$ cat hello/greeting.txt
Hello, Joe.
Glad to meet you.
```

## Intermediate: a small project scaffold

A more realistic single-language scaffold. Optional tests, a license picker, a templated source file, and a loop for module subdirectories.

```
ask project_name "Project name?" string
ask use_tests "Include a test suite?" bool default false
ask num_modules "How many top-level modules?" int default 1
ask license "License?" string options "MIT" "Apache-2.0" "GPL-3.0" default "MIT"

let slug = lower(replace(trim(project_name), " ", "-"))
let dir = slug + "-project"

mkdir "{dir}" as project

file project/"README.md" content "# {project_name}

Licensed under {license}.
"

file project/"LICENSE" from "licenses/{license}.txt"

repeat num_modules as i
  mkdir project/"src/module_{i}"
  file project/"src/module_{i}/main.cpp" from "templates/main.cpp"
end

if use_tests
  mkdir project/"tests"
  file project/"tests/test_main.cpp" from "templates/test_main.cpp"
  file project/"tests/CMakeLists.txt" content "add_subdirectory(tests)
"
end
```

What this shows:

- `ask` with `options` for a bounded answer, with a `default`.
- A `let` chain that derives a directory name from the project name.
- Path interpolation (`"{license}.txt"`) selecting one of several source files.
- A `repeat` over an `int` answer, with `{i}` interpolation in paths and content.
- An `if` block grouping three statements that share a single condition.
- A `from` clause loading static template files from the bundle.

If `use_tests` is `false`, no `tests/` directory is created. If `num_modules` is `0`, the loop body runs zero times.

## Realistic: a multi-language scaffold

A fuller template using most of the language: `if`, `repeat`, `mkdir from`, `copy into`, `run`, `include`, the alias-then-append idiom, and conditional aliases.

```
ask project_name "Project name?" string
ask language "Primary language?" string options "python" "rust" "go" default "python"
ask use_tests "Tests?" bool default true
ask use_docs "Docs?" bool default false
ask use_ci "CI workflow?" bool default false
ask use_git "Initialise git?" bool default true
ask use_claude "Claude Code config?" bool default false

ask num_modules "Number of modules?" int default 1

let slug = lower(replace(trim(project_name), " ", "-"))
let dir = slug + "-project"

mkdir "{dir}" as project

# Base from a language-specific template.
mkdir project/"src" from "templates/{language}/src" as src_path

# Optional add-ons merged into the base.
if use_tests
  mkdir project/"tests" as tests_path when use_tests
  copy "templates/{language}/tests" into tests_path when use_tests
end

if use_docs
  mkdir project/"docs" from "templates/docs"
end

if use_ci
  mkdir project/".github/workflows"
  file project/".github/workflows/ci.yml" from "templates/{language}/ci.yml"
end

# README built up from optional sections via the alias-then-append idiom.
file project/"README.md" content "# {project_name}

A {language} project.
" as readme

file readme append content "
## Tests

Run \\`make test\\` to run the test suite.
" when use_tests

file readme append content "
## Docs

Build with \\`make docs\\`.
" when use_docs

# Modules: one subdirectory per module, generated in a loop.
repeat num_modules as i
  mkdir src_path/"module_{i}"
  file src_path/"module_{i}/README.md" content "# Module {i}
"
end

# Run-time setup, gated by user consent and pinned to the project directory.
run "git init" in project when use_git
run "git add ." in project when use_git
run "git commit -m \\"initial\\"" in project when use_git

# Compose with an installed sub-template.
include claude_setup when use_claude
```

What this shows:

- Several boolean asks at the top, all with sensible defaults so the user can press enter through most of them.
- `mkdir from` to seed a base directory from a language-specific template tree.
- `copy into` to merge optional add-ons into a directory created earlier in the run.
- The alias-then-append idiom for the README: the base is unconditional, additional sections append only when their gate is true.
- A `repeat` that uses an alias declared earlier (`src_path`) as the parent of each module directory.
- Three `run` commands, all pinned to `project` via `in`, all gated on `use_git`.
- An `include` for shared Claude config, triggered only when `use_claude` is `true`.

A trust prompt summary for this template would list the three `git` commands so the user can authorise them before the run starts.

## Notes on the examples

Every path expression in the examples uses quoting deliberately:

- `project` (no quotes): an alias declared by an earlier `as` clause.
- `"{language}"` (quoted with interpolation): a literal segment whose value depends on the answer.
- `src_path/"module_{i}"`: alias followed by quoted segment with interpolation.

`{language}` works inside the quoted source paths (`"templates/{language}/src"`) because path interpolation requires a quoted segment. See @ref lang_paths "Path expressions" for the rules.

The README example uses real line breaks inside the string literal rather than escape sequences, because spudlang strings do not interpret `\n` and similar. The interior backslash-quote pairs (`\\` `"`) inside the `run` command examples are bytes `\` and `"`, which are what the shell sees: those escape sequences are processed by `/bin/sh`, not by spudlang.

## See also

- @ref lang_best_practices "Best practices": the patterns these examples follow, with reasoning.
- @ref lang_pitfalls "Pitfalls": the mistakes these examples avoid.
- @ref lang_reference "Language Reference": the strict reference for every clause used here.
