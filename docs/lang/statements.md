# Statements {#lang_statements}

A spudlang program is a sequence of statements that the interpreter executes top to bottom. There are nine statement kinds, falling into four loose groups:

| Group        | Statements                          |
|--------------|-------------------------------------|
| Prompts      | `ask`                               |
| Variables    | `let`, reassignment                 |
| Filesystem   | `mkdir`, `file`, `copy`             |
| Side effects | `run`, `include`                    |
| Control flow | `if`, `repeat`                      |

Filesystem actions are queued during execution and flushed at the end of the run. `run` commands and `include` subprocesses execute in source order alongside the queued filesystem operations during the flush. See @ref lang_reference "Language Reference" for the high-level execution model.

[TOC]

## ask

```
ask <name> "<prompt>" <type> [options <v1> <v2> ...] [default <value>] [when <condition>]
```

Declares a variable `<name>` and prompts the user at run time.

| Clause       | Effect                                                                              |
|--------------|-------------------------------------------------------------------------------------|
| `options`    | Restrict valid answers to a fixed list, presented as a numbered menu                |
| `default`    | Value used when the user submits an empty response                                  |
| `when`       | Skip the prompt entirely if the condition is false; the default is bound instead    |

Rules:

- A question is **required** unless it has a `default`.
- A `when`-gated question must have a `default`. The default is bound when the gate is false, so later code always sees a real value.
- `default` accepts any expression of the matching type, including references to earlier variables and function calls. Mismatches against the declared type are caught at parse time for literal defaults and at validate time for identifier-or-expression defaults whose type can be inferred.
- When both `options` and `default` are present, the default value must equal one of the listed options.
- Inside a `repeat` body, an `ask` runs once per iteration. The bound value lives only for that iteration.

Types accepted on `ask`:

| Type     | Accepted input                                                                                  |
|----------|--------------------------------------------------------------------------------------------------|
| `string` | Any text                                                                                         |
| `bool`   | `y`, `yes`, `true`, `n`, `no`, `false`, case-insensitive. The hint shows `[Y/n]` for `default true`, `[y/N]` for `default false`, `[y/n]` otherwise |
| `int`    | Decimal integer                                                                                  |

Examples:

```
ask project_name "What is the project name?" string
ask use_tests "Include a test suite?" bool default false
ask license "License?" string default "MIT"
ask num_weeks "How many weeks?" int default 0 when use_tests
ask format "Output format?" string options "pdf" "html" "latex" default "pdf"
ask postgres_version "Postgres version?" int options 15 16 17

ask project_name "Project name?" string
ask slug "Slug?" string default lower(trim(project_name))
```

When an `ask` is inside a `repeat` body, prompts are indented two spaces per nesting level. The `(N/M)` static-position counter sits next to an `iteration K of L` indicator, for example `(3/7, iteration 2 of 4)`.

## let

```
let <name> = <expression>
```

Declares a new variable bound to the value of an expression. The variable's type is inferred from the right-hand side and is fixed for the lifetime of the binding.

```
let slug = lower(trim(project_name))
let kebab = lower(replace(trim(name), " ", "-"))
let project_dir = slug + "-project"
let total_days = num_weeks * 7
```

Names declared by `let` cannot collide with any visible binding (`ask` answers, other `let` names, repeat iterators, path aliases). Shadowing is rejected.

## Reassignment

```
<name> = <expression>
```

Replaces the value of an existing `let` binding. Useful for accumulators inside a `repeat` body.

```
let total = 0
let n = 3
repeat n as i
  total = total + i
end
```

Rules:

- The name must already be declared by `let` and visible in the current scope. Reassigning an undeclared name is a validation error.
- The new value's type must match the original binding.
- Only `let` bindings are mutable. `ask` answers, path aliases (`as <name>`), and repeat iterators are read-only.
- Reassignments inside a `repeat` body modify the outer `let` binding, supporting accumulator patterns.

The interpreter captures values at statement time. Between two statements that both read `n`, a reassignment in between is observed by the second statement only.

## mkdir

```
mkdir <path> [from <source>] [verbatim] [mode <octal>] [when <condition>] [as <name>]
```

Creates a directory at `<path>`, with `mkdir -p` semantics (intermediate directories are created automatically).

| Clause      | Effect                                                                              |
|-------------|-------------------------------------------------------------------------------------|
| `from`      | Populate the new directory from `<source>`, recursively                             |
| `verbatim`  | With `from`, suppress `{ident}` substitution in copied file contents                |
| `mode`      | Set directory permissions, masked to `0o0777`                                       |
| `when`      | Skip creation if the condition is false                                             |
| `as`        | Bind the resolved path to a name for later reuse                                    |

Examples:

```
mkdir "static"
mkdir "static" as static_path
mkdir static_path/"notes"
mkdir "tests" when use_tests
mkdir "private" mode 0700
mkdir "templates" from "base_templates" as template_path
mkdir "templates" from "base_templates" verbatim when use_templates as template_path
```

`mkdir` and `copy` are the two ways to populate a directory. `mkdir from` creates a new directory; `copy ... into` merges into an existing one.

## file

```
file <path> [append] from <source> [verbatim] [mode <octal>] [when <condition>] [as <name>]
file <path> [append] content <expression>     [mode <octal>] [when <condition>] [as <name>]
```

Creates or appends to a file. The two forms differ only in where the content comes from.

| Clause      | Effect                                                                              |
|-------------|-------------------------------------------------------------------------------------|
| `append`    | Append to the file instead of overwriting; the file must already have been created in this run, or the operation fails |
| `from`      | Read the source file from the bundle. `{ident}` substitution applies unless `verbatim` |
| `content`   | Use the value of an inline expression. Full `{expr}` interpolation applies          |
| `verbatim`  | With `from`, suppress all substitution                                              |
| `mode`      | Set file permissions, masked to `0o0777`                                            |
| `when`      | Skip creation if the condition is false                                             |
| `as`        | Bind the resolved path; primarily useful for conditional `append`                   |

Examples:

```
file "{dir}/README.md" content "# " + project_name
file "{dir}/.gitignore" from "templates/gitignore"
file "{dir}/run.sh" from "templates/run.sh" mode 0755
file "{dir}/main.tex" from "templates/main.tex" verbatim when format == "latex"
file "{dir}/log.txt" append content "Created by spudplate\n"
```

Conditional append via alias:

```
file "{dir}/README.md" content "# " + project_name as readme
file readme append content "## Notes\n" when use_notes
file readme append content "## Testing\n" when use_tests
```

Without `append`, the second `file` on the same path overwrites the first. With `append`, the file must have been created in the current run by an earlier `file` statement.

## copy

```
copy <source> into <destination> [verbatim] [when <condition>]
```

Copies the contents of `<source>` into an **already-existing** `<destination>` directory. Errors if the destination does not exist (use `mkdir from` for that case). The right tool for merging multiple sources into one directory.

| Clause     | Effect                                                                              |
|------------|-------------------------------------------------------------------------------------|
| `verbatim` | Suppress `{ident}` substitution in copied file contents                             |
| `when`     | Skip the copy if the condition is false                                             |

Examples:

```
mkdir "templates" from "base_templates" as template_path
copy "philosophy_templates" into template_path when use_philosophy
copy "programming_templates" into template_path when use_programming
copy "assets" into static_path/"assets" verbatim
```

## repeat

```
repeat <int_var> as <iter> [when <condition>]
  ... statements ...
end
```

Runs the block `<int_var>` times. The iterator `<iter>` is bound to the current iteration index (0-based). Nested `repeat`, `if`, `ask`, and any action statement may appear inside the body.

The optional `when` clause skips the entire loop if the condition is false.

```
ask num_modules "How many modules?" int default 0
repeat num_modules as i when num_modules > 0
  mkdir "module_{i}"
  file "module_{i}/README.md" content "# Module " + i
end
```

Scoping rules:

- A `repeat` body introduces a new scope. The iterator and any `let` or `as` declared inside the block are local to that body.
- References from outside the body to inner names are rejected.
- A `let` or `as` inside the body whose name collides with any visible outer binding is rejected as a shadowing error.

See @ref lang_scoping "Scoping and conditions" for the full rules.

## if

```
if <condition>
  ... statements ...
end
```

Runs the body when `<condition>` evaluates to `true`. The condition uses the same expression grammar as `when`. Nested `if` and `repeat`, `ask`, and any action may appear inside.

There is no `else` or `else if`. Compose conditional branches using nested `if` blocks. An `if` block does **not** itself accept a `when` clause: the `if` is the gate. Inner statements may still carry their own `when`.

`ask` inside `if`:

- An `ask` inside an `if` body without its own `when` clause does not need a `default`. The `if` is the gate.
- An `ask` inside an `if` body that carries its own `when` clause **does** need a `default`, because the inner gate can be false while the outer `if` is true.

```
ask use_tests "Include a test suite?" bool default false
if use_tests
  mkdir "tests"
  file "tests/README.md" content "# Tests"
  ask test_runner "Test runner?" string options "ctest" "gtest" default "ctest"
end
```

## include

```
include <name> [when <condition>]
```

Runs another installed spudplate template by name, as an **independent subprocess**. The included template has its own questions, its own filesystem operations, and no shared scope with the caller.

The named template must be installed (or, when the caller is itself bundled into a `.spp`, the named template must have been bundled into the caller's `_deps/` slot at export time).

```
ask use_claude "Set up Claude config?" bool default false
include claude_setup when use_claude
```

There is no source-level inlining. All template reuse goes through `include` and the installed registry.

## run

```
run <expression> [in <path>] [timeout <int>] [when <condition>]
```

Runs a shell command via `/bin/sh -c` after the user has authorised the template (see [Trust prompt](#trust-prompt) below). The expression evaluates to a string at run time and the resulting command is queued for execution at the flush step alongside the filesystem operations, in source order.

| Clause      | Effect                                                                              |
|-------------|-------------------------------------------------------------------------------------|
| `in`        | Pin the working directory. Without it, the command inherits the cwd of the `spudplate` process |
| `timeout`   | Per-statement timeout in seconds. Default 60. The CLI flag `--no-timeout` overrides every per-statement value |
| `when`      | Skip the command if the condition is false                                          |

Output streams live to the parent process's stdout and stderr. A short banner `running '...' (timeout Ns)` (or `(no timeout)`) is printed to stderr before each command to make long-running commands visible.

Examples:

```
ask project "Project name?" string
mkdir "{project}" as proj
run "git init" in proj
ask repo_url "Origin URL?" string default ""
run "git remote add origin " + repo_url in proj when repo_url != ""
run "npm install" in proj timeout 600
```

### Trust prompt {#trust-prompt}

Every `spudplate run <file.spud>` invocation that contains any `run` clauses prompts the user once, before any statement executes:

```
This template will execute the following shell commands via /bin/sh:
  1. "git init"
  2. "git remote add origin " + repo_url
Authorise these commands? [y/N]
```

The summary lists each command's source-form expression. Conditional clauses appear regardless of the eventual `when` outcome (the validator cannot predict run-time values). Commands inside `repeat` bodies are flagged as such.

Declining aborts cleanly: no prompts run, no files are written, no commands execute.

The `--yes` (or `-y`) flag bypasses the prompt for non-interactive callers. Once `spudplate install` records install-time consent, installed templates run without re-prompting.

### Failure handling

A non-zero exit, a signal-killed process, or a failure to invoke the shell raises a runtime error and aborts the rest of the flush. Filesystem operations queued **before** the failed command have already been performed and remain on disk. Later operations do not run.

### Timeouts

On expiry the interpreter sends `SIGTERM` to the command's process group, waits up to 5 seconds for clean exit, then sends `SIGKILL`. The command is reported as a runtime error and the deferred flush is aborted.

### Security caveats

- **Shell injection through interpolation.** The trust prompt shows the literal source expression. The evaluated string is what executes. `run "git clone " + url` looks safe in the prompt, but a malicious `url` such as `; rm -rf $HOME` is still passed through. Treat any value flowing into a `run` command as untrusted: quote inputs in the command string, or restrict input via `options`.
- **Working directory.** Without `in <path>`, commands inherit the cwd of `spudplate`, not a per-template sandbox. Templates that create a project subdirectory should pin every following command to it via `in <path>`.
- **Interactive commands.** A command that takes over the terminal (an editor, an interactive REPL) does so as the parent process. The flush blocks until the command exits or its timeout fires.
- **No allowlist.** Any command is permitted once authorised. Privilege escalation prompts (such as `sudo`) ask for the user's password, which is the natural friction for destructive operations.

See @ref lang_pitfalls "Pitfalls" for guidance on writing `run` clauses defensively.
