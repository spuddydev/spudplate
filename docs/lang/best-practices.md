# Best Practices {#lang_best_practices}

This page covers patterns that recur in the templates that work well, alongside the reasoning that motivates each one. Spudlang is small enough that there are not many ways to do most things, but the choices that exist matter for readability and for keeping templates from breaking when they grow.

The advice here is **descriptive first**: it explains why most useful templates settle on certain idioms, not laws you must obey. Where there really is one right answer (the no-shadowing rule, for example), that comes from the language and is documented in the reference; this page rarely repeats it.

[TOC]

## Names

### Use snake_case for everything

Spudlang identifiers are case-sensitive, but the test suite, the standard library of examples, and every existing real template uses `snake_case`. Mixing styles within one template is jarring, and `camelCase` reads oddly next to keywords like `mkdir` and `as`. Pick `snake_case` and stay there.

```
ask project_name "Project name?" string
let kebab_slug = lower(replace(trim(project_name), " ", "-"))
```

### Name `ask` variables after the value, not the question

The variable holds the answer, not the prompt. Aim for names that describe what the value **is**, not what was asked.

```
ask use_tests "Include a test suite?" bool default false      # value is whether to use tests
ask num_weeks "How many weeks?" int default 0                  # value is the count
ask format "Output format?" string options "pdf" "html"        # value is the chosen format
```

Names that read as a question (`should_we_include_tests`, `how_many_weeks`) age badly: they make `if use_tests` read fine but `if should_we_include_tests` clunky.

### Boolean gates start with `use_` or are simply the feature name

Most boolean asks are gates on optional sections. The conventional prefixes make the intent obvious at a glance:

```
ask use_tests "Tests?" bool default false
ask use_docs "Docs?" bool default false
ask use_ci "CI workflow?" bool default false
```

A direct feature name (`tests`, `docs`) also works for the boolean form. Pick one form and stay consistent.

## Asking questions

### Always supply a `default` if the answer is optional

A question with no default is **required**. The user cannot skip it. That is exactly right for the project name, but wrong for the test runner: a user who does not care should be able to press enter.

A useful rule of thumb: if you can describe a sensible default in one sentence, the question deserves a `default`.

### Use `options` to bound free-form answers

`options` turns an answer into a numbered menu. The user can type the literal value or the menu number. Two reasons this is worth using even when typing the value is easy:

1. The validator can check that a `default` matches the options at parse time, catching typos in the template.
2. The user discovers the legal values without having to read the docs.

Anywhere you would write a comment like "use 'pdf', 'html', or 'latex'", `options` does the same job in a way the user will see.

### Order questions to match the dependencies between them

Because `ask` is a regular statement, later questions can use the answers of earlier ones in their `default` and `when` clauses. Order them so that prerequisites come first:

```
ask use_tests "Tests?" bool default false
ask test_runner "Test runner?" string options "ctest" "gtest" \
    default "ctest" when use_tests
ask num_weeks "How many weeks?" int default 0 when use_tests
```

A well-ordered question sequence reads like a guided conversation.

### Gate dependent questions with `when`

A `when`-gated question is only asked if its condition is true. The default is bound when the gate is false, so subsequent code always sees a real value. This is cleaner than asking a question the user has already implicitly declined.

The `when`-gated question must always have a `default`; the validator enforces it.

## Variables

### `let` once, use everywhere

If the same expression appears in two places, name it with `let`. It gives you one place to change, and a name that documents what the value means.

```
let slug = lower(replace(trim(project_name), " ", "-"))
let project_dir = slug + "-project"

mkdir project_dir
file project_dir/"README.md" content "# " + project_name
```

### Prefer `as` aliases over inline path expressions you reuse

When the same path appears twice in a row, an `as` alias is shorter and conveys "this is the same path, not a coincidentally-similar one":

```
mkdir "{dir}/static" as static_path
mkdir static_path/"images"
mkdir static_path/"fonts"
```

versus

```
mkdir "{dir}/static"
mkdir "{dir}/static/images"
mkdir "{dir}/static/fonts"
```

Both work; the alias form scales better as the path gets deeper or more complex.

### The alias-then-append idiom

A README built from optional sections is the canonical use of `file ... as`:

```
file "{dir}/README.md" content "# " + project_name as readme
file readme append content "\n\n## Notes\n" when use_notes
file readme append content "\n\n## Testing\n" when use_tests
file readme append content "\n\n## CI\n" when use_ci
```

This is the lightest-weight way to build a sectioned file with optional parts. The same pattern works for `.gitignore`, `package.json` `scripts` blocks, etc.

### Keep `let` chains short

A `let` derived from a `let` derived from a `let` works, but each link in the chain hides the original input. Three or four steps is fine; a long chain usually means the original `ask` should be reshaped.

## Paths

### Quote literal segments

A bare path segment is a variable reference. A quoted segment is a literal. When in doubt, quote.

```
mkdir "src"               # literal directory called "src"
mkdir src                 # variable reference to 'src' (must be a let or alias)
```

A common mistake is to write `mkdir templates from base_templates` and expect both names as literals. The parser interprets each as a variable reference, and the validator rejects them as undeclared. Quote them: `mkdir "templates" from "base_templates"`.

### Use `{var}` only inside quoted segments

Interpolation in a path expression works only inside a quoted segment:

```
mkdir "week_{n}"            # ok
mkdir "{prefix}/notes"       # ok
mkdir week_{n}              # parse error
```

If you want to combine an alias with an interpolation, quote the part that needs braces:

```
mkdir "static" as static_path
mkdir static_path/"week_{n}"
```

### `mkdir from` vs `copy into`

These two answer different questions:

| Goal                                                     | Statement                          |
|----------------------------------------------------------|------------------------------------|
| Create a new directory and populate it from one source    | `mkdir <path> from <source>`       |
| Add content to a directory you have already created       | `copy <source> into <path>`        |

A common idiom uses both. Create the base from one tree, then merge optional add-ons:

```
mkdir "{dir}/templates" from "base_templates" as templates_path

copy "templates_python" into templates_path when language == "python"
copy "templates_rust" into templates_path when language == "rust"
copy "templates_extras" into templates_path when use_extras
```

## Conditional structure

### `when` for one statement, `if` for blocks

A single conditional statement reads fine with a `when` clause:

```
mkdir "tests" when use_tests
```

Three or more statements with the same `when` start to repeat. An `if` block is clearer:

```
if use_tests
  mkdir "tests"
  file "tests/README.md" content "# Tests"
  file "tests/test_main.cpp" from "templates/test_main.cpp"
end
```

The threshold is usually two or three statements. Below that, `when` is fine. Above it, the repeated condition obscures what is shared.

### Compose two-branch decisions from negated `if`s

Spudlang has no `else`. For "do A or B but not both", two `if` blocks work:

```
if use_postgres
  copy "templates/postgres" into "{dir}/db"
end
if not use_postgres
  copy "templates/sqlite" into "{dir}/db"
end
```

For more than two branches, a chain of `if` blocks with mutually-exclusive `==` conditions is the cleanest form.

### Keep `repeat` bodies short

A `repeat` introduces its own scope and prompts inside it run once per iteration. Both are useful, but a long loop body with several `let`s and nested `if`s gets hard to follow. If you find yourself writing more than ten or so lines inside a `repeat`, consider whether each iteration should be its own `include`d template.

## Files and content

### `file from` for large static content, `file content` for small dynamic content

`file from` is for files where most of the content is fixed and you want to ship them as-is, with light `{ident}` substitution.

`file content` is for short dynamic strings: a one-line header, a generated config blob.

The line is roughly: if you would think of opening the file in a text editor, it belongs in `from`. If it is a single string the template assembles, `content` is right.

### Use `verbatim` for binary content and for text that contains literal `{`

The interpreter auto-detects binary content (anything that is not valid UTF-8) and copies it verbatim regardless of the `verbatim` keyword. So you do not need `verbatim` on PNGs, favicons, etc.

You **do** need `verbatim` on text files that legitimately contain `{`, because the substitution scan would otherwise misinterpret them. LaTeX templates and shell scripts are common cases.

### Set `mode` only when the system default is wrong

Most files want the system default. The two cases worth setting `mode` for are:

- Executable scripts: `mode 0755`.
- Files that hold secrets or per-user state: `mode 0600` or `mode 0700`.

Setting `mode 0644` everywhere is noise; that is what your umask gives you anyway.

## Running shell commands

### Pin every command to a working directory

Without `in <path>`, a `run` inherits the cwd of `spudplate`, which is rarely the project subdirectory the template just created. Pin every command:

```
mkdir "{dir}" as proj
run "git init" in proj
run "git add ." in proj
run "git commit -m 'initial'" in proj
```

### Restrict free-form inputs that flow into `run`

A user's `string` answer can be anything, including a shell injection payload. Two ways to defend:

- **Bound the input.** If the value should be one of a known list, use `options`.
- **Scrub the input.** If the value cannot be bounded (a URL, a project name), validate the shape with the application that consumes it; do not just paste it into a shell command.

Most templates do not need user input inside a `run`. The trust prompt and the user reading their own commands are a good safety net, but the safest design avoids the problem.

### Use `timeout` for slow commands

The default timeout is 60 seconds, which is right for `git init` but wrong for `npm install` or a multi-minute build. Set `timeout` explicitly when you know the command will take longer.

### Reach for `run` only when there is no native form

`run` is the most powerful and the most dangerous statement. Use it for things that cannot be expressed structurally:

- `git init`, `git remote add origin <url>`
- `npm install`, `cargo new`, `python -m venv`
- `chmod +x` (although `mode 0755` on `file from` does the same thing without the shell)

If a `run` is doing what `file content`, `mkdir from`, or `copy into` could do, prefer the structural form. It is faster, fails earlier, and does not need the trust prompt.

## Composing templates

### Factor shared subroutines into their own templates

If two templates set up Claude Code config, do not copy the lines into both. Make a `claude_setup` template, install it, and `include` it from each:

```
ask use_claude "Set up Claude config?" bool default false
include claude_setup when use_claude
```

The included template runs inline at the include point, asks its own questions in source order, and stays maintainable independently. Its bytes are bundled into the parent at install time, so the recipient does not need the dependency installed separately.

### Keep includes opt-in

An `include` without a `when` runs unconditionally, asking its own questions every time. Most include candidates are optional, so a `when` paired with a `bool` ask is the conventional shape.

## Errors and validation

### Prefer parse-time errors to run-time errors

Most spudlang errors surface before any prompt runs. The validator checks types, scopes, alias conditions, and condition normalisation. A template that fails validation is one the user never has to interact with.

A few categories of error are inherently run-time:

- A `from` source that is missing at install time (validate with `spudplate validate`).
- A `copy into` whose destination does not exist (often a logic error in the template).
- A `run` command that returns non-zero (the user's environment is the variable).

Lean on the parse-time guarantees: a template that survives `spudplate validate` is much closer to working than one that has not been validated.

### Use `spudplate validate` while writing

`spudplate validate <file.spud>` runs the lexer, parser, and validator without prompting or installing. Run it after each substantive edit. The errors it produces are precise about line and column.

## See also

- @ref lang_examples "Examples": fully-worked templates that exercise these patterns.
- @ref lang_pitfalls "Pitfalls": specific mistakes that are easy to make.
- @ref lang_reference "Language Reference": the strict reference for everything described informally here.
