# Pitfalls {#lang_pitfalls}

A small language has small pitfalls. The ones below are the cases where spudlang's behaviour will surprise an author who has not seen the rule. Most surface as parse or validate errors, so the language tells you about them, but the error message can be cryptic if you have not met the underlying rule.

The reference pages cover the rules in their own context; this page is the consolidated list of "things that bite people".

[TOC]

## Reserved words cannot be unquoted path segments

The lexer recognises every reserved word (full list in @ref lang_lexical "Lexical Structure") before path-expression parsing. As a result, a directory named the same as a keyword (`include`, `from`, `as`, `end`, `file`, `mkdir`, etc.) **cannot appear unquoted** in a path expression.

```
mkdir project_dir/include            # parse error: 'include' is the include keyword
mkdir "project_dir/include"          # ok, the whole path is a quoted literal
mkdir "project_dir" as base
mkdir base/"include"                 # ok, mixed alias and quoted segment
```

The error you will see is "expected newline after mkdir statement", pointing at the offending keyword. The fix is always to quote the segment.

This affects more directory names than it might first seem: `include` is a normal C/C++ headers folder, `file` shows up in some toolchains, `from` is rare but legal as a folder name. When in doubt, quote.

## String literals do not process escape sequences

The lexer reads everything between two `"` characters as literal bytes. There is **no** escape mechanism: a backslash followed by `n` inside a string literal is two characters, not a newline.

```
file "out.txt" content "line1\\nline2"    # writes the 11 bytes literally,
                                           # does NOT write two lines
```

To put a real newline in a string literal, embed an actual line break:

```
file "out.txt" content "line1
line2
"
```

There is also no `\"` escape. A double-quote inside a string literal closes the string. To compose a string that contains `"`, bind the value via `ask` or `let`.

## `{var}` outside quoted path segments is rejected

Inside a path expression, `{...}` interpolation is allowed **only inside a quoted segment**:

```
mkdir "week_{n}"             # ok
mkdir "{prefix}/notes"       # ok
mkdir week_{n}               # parse error
mkdir {prefix}/notes         # parse error
```

The error is "'{...}' interpolation is only allowed inside quoted path strings". To combine an alias with an interpolation, quote the part that needs braces:

```
mkdir "static" as static_path
mkdir static_path/"week_{n}"
```

This rule is for paths only. Inside string literals (`content`, `default`, `run`), `{expr}` interpolation works directly.

## Commutativity is not recognised in alias conditions

A path alias bound under a `when` clause must be referenced under an equivalent condition. The validator normalises bool conditions before comparing them: `b`, `b == true`, and `not not b` all match. But `and` and `or` are **not** considered commutative:

```
mkdir "x" as x_path when use_a and use_b
file x_path/"y" content "" when use_a and use_b   # ok
file x_path/"z" content "" when use_b and use_a   # error: not recognised as equivalent
```

The fix is to keep the operands in the same order on every reference, or to bind the compound condition to a `let` first:

```
let both = use_a and use_b
mkdir "x" as x_path when both
file x_path/"y" content "" when both
file x_path/"z" content "" when both
```

This is a known limitation. It may be lifted in a future version; for now, write the same condition the same way every time.

## A `when`-gated `ask` must have a `default`

A bare `ask` is required: the user cannot skip it. A `when`-gated `ask` is asked only sometimes, but the variable must be bound after the statement either way. So the validator requires a `default`:

```
ask num_weeks "Weeks?" int when use_tests                  # error: missing default
ask num_weeks "Weeks?" int default 0 when use_tests        # ok
```

When `use_tests` is `false`, the default is bound and `num_weeks` is ready for any later code that reads it. When `use_tests` is `true`, the user is prompted as normal, and the default fills in for empty input.

## `copy into` errors if the destination does not exist

`copy <source> into <dest>` requires `<dest>` to already exist. It will not create it for you. If you want a fresh directory populated from one source, use `mkdir from` instead.

```
mkdir "project" as proj
copy "templates" into proj             # ok, proj exists
copy "templates" into "missing_dir"    # runtime error: missing_dir does not exist
```

The pattern that combines both: `mkdir from` to create and seed, then `copy into` to merge add-ons.

## `file append` errors if the file is not from this run

The interpreter tracks paths created during a single run. `file ... append` requires the target file to have been created earlier in the **same run**. Appending to a pre-existing file from outside the run is rejected on principle: spudplate never modifies files it did not create.

```
file "a.txt" content "first"
file "a.txt" append content "more"      # ok, both in this run

file "/etc/hosts" append content "..."   # runtime error: file pre-existed
```

This is also why an aliased file path (`as`) is the conventional way to wire up conditional appends: the alias makes it impossible to typo a different filename.

## Shell injection through `run` interpolation

`run` builds a shell command from a string expression and dispatches it via `/bin/sh -c`. The trust prompt shows the **literal source** but the **evaluated string** is what executes:

```
run "git clone " + url
```

A malicious `url` such as `; rm -rf $HOME` is passed straight to the shell. Defend by:

- using `ask ... options ...` to bound the input, or
- validating the input shape (a URL by attempting to parse it; a project name by rejecting characters that would not survive a filesystem write anyway).

See @ref lang_stmt_run "run" for the full security treatment.

## `run` without `in <path>` uses the parent's working directory

Without `in`, a `run` inherits the working directory of `spudplate`, which is wherever the user invoked it from (rarely the project subdirectory the template just created). Always pin commands that depend on a particular cwd:

```
mkdir "{dir}" as proj
run "git init" in proj
run "git add ." in proj
```

A command that runs in the wrong directory often fails in confusing ways. Pinning is cheap.

## No rollback after a flush failure

Filesystem operations are queued during execution and flushed at the end. If the flush starts and operation N out of M fails, operations 1 through N-1 have **already** been performed and remain on disk. There is no rollback.

This usually only matters for `run` commands: a `run` failing mid-flush leaves the filesystem in a partial state. The `mkdir`, `file`, and `copy` operations in the queue rarely fail at flush time, since paths are validated before the flush starts.

The "all or nothing" guarantee covers the common case: a user aborting at any prompt, before the flush starts. The first prompt is the cheapest place to back out.

## `--no-timeout` is a CLI flag, not a per-statement clause

The default `run` timeout is 60 seconds. A per-statement `timeout 600` overrides it for one command. There is no per-statement way to say "no timeout"; the only escape is the CLI `--no-timeout` flag, which applies to every `run` in the invocation.

If a single command genuinely needs no limit (an interactive editor, an indefinite watcher), the user has to invoke `spudplate run --no-timeout`.

## See also

- @ref lang_lexical "Lexical Structure": the keyword list and string-literal reading rules.
- @ref lang_paths "Path expressions": the path grammar rules.
- @ref lang_scoping "Scoping": condition normalisation and alias conditions.
- @ref lang_stmt_run "run": the run statement and its security caveats.
- @ref lang_best_practices "Best practices": the patterns that avoid most of these.
