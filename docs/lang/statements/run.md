# run {#lang_stmt_run}

```
run <expression> [in <path>] [timeout <int>] [when <condition>]
```

Runs a shell command via `/bin/sh -c` after the user has authorised the template (see [Trust prompt](#trust-prompt) below). The expression is evaluated to a string at run time and the resulting command is queued for execution at the flush step alongside the filesystem operations, in source order.

`run` is the most powerful and the most dangerous statement. It is the only one that can have side effects outside the template's own filesystem operations.

[TOC]

## Clauses

| Clause      | Effect                                                                              |
|-------------|-------------------------------------------------------------------------------------|
| `in`        | Pin the working directory. Without it, the command inherits the cwd of the `spudplate` process |
| `timeout`   | Per-statement timeout in seconds (positive integer). Default 60. The CLI flag `--no-timeout` overrides every per-statement value |
| `when`      | Skip the command if the condition is false                                          |

## Examples

```
ask project "Project name?" string
mkdir "{project}" as proj

run "git init" in proj
run "git remote add origin " + repo_url in proj when repo_url != ""
run "npm install" in proj timeout 600
```

A short banner of the form `running '...' (timeout Ns)` (or `(no timeout)`) is printed to stderr before each command, so a long-running command does not look frozen. Output streams live to the parent process's stdout and stderr.

## in path

Without `in`, the command inherits the cwd of the `spudplate` process, which is rarely the project subdirectory the template just created. Pin every command that needs a particular working directory:

```
mkdir "{dir}" as proj
run "git init" in proj
run "git add ." in proj
run "git commit -m 'initial'" in proj
```

The path is a regular path expression. Alias references and `{expr}` interpolation work. The directory must exist by the time the command runs, otherwise the run errors. The cwd is restored after the command, even on failure.

## timeout

`timeout` accepts a positive integer of seconds. If the command runs longer, the interpreter sends `SIGTERM` to its process group, waits up to 5 seconds for clean exit, then sends `SIGKILL`. The command is reported as a runtime error and the deferred flush is aborted.

```
run "npm install" in proj timeout 600
run "build.sh" in proj timeout 1800
```

Without `timeout`, the run inherits the default of **60 seconds**. The CLI flag `--no-timeout` removes timeouts entirely for the invocation; this overrides every per-statement value, so users who genuinely want to run an interactive editor or a multi-hour build can do so.

## Trust prompt {#trust-prompt}

Every `spudplate run <file.spud>` invocation that contains any `run` clauses prompts the user once, before any statement executes:

```
This template will execute the following shell commands via /bin/sh:
  1. "git init"
  2. "git remote add origin " + repo_url
Authorise these commands? [y/N]
```

The summary lists each command's source-form expression. Conditional clauses appear regardless of the eventual `when` outcome, since the validator cannot predict run-time values. Commands inside `repeat` bodies are flagged as such.

Declining aborts cleanly: no prompts run, no files are written, no commands execute.

The `--yes` (or `-y`) flag bypasses the prompt for non-interactive callers. Once `spudplate install` records install-time consent, installed templates run without re-prompting.

## Failure handling

A non-zero exit, a signal-killed process, or a failure to invoke the shell raises a runtime error and aborts the rest of the flush. Filesystem operations queued **before** the failed command have already been performed and remain on disk. Later operations do not run.

This is the same partial-failure model as the rest of the flush: spudplate makes no attempt to roll back operations that have already succeeded.

## Security caveats

The trust prompt and the `--yes` flag are user-facing safety nets. Inside the template, the author still has to be careful.

### Shell injection through interpolation

The trust prompt shows the **literal source expression**. The **evaluated string** is what executes.

```
run "git clone " + url
```

looks safe in the prompt, but a malicious `url` answer such as `; rm -rf $HOME` is still passed through to the shell.

Treat any value flowing into a `run` command as untrusted. Two ways to defend:

- **Restrict input.** Use `ask ... options ...` to bound the value to a known-safe set.
- **Quote inputs.** Build the command so the user value is inside single quotes that the shell will not re-interpret. (Single quotes inside the user value defeat this; chained interpolation is safer than naive concatenation.)

### Working directory

Without `in <path>`, commands inherit the cwd of `spudplate`, not a per-template sandbox. A template that creates a project subdirectory should pin every following command to it via `in <path>`.

### Interactive commands

A command that takes over the terminal (an editor, an interactive REPL) does so as the parent process. The flush blocks until the command exits or its timeout fires. This is rarely what you want from a scaffolding template.

### No allowlist

Any command is permitted once authorised. Privilege escalation prompts (such as `sudo`) ask for the user's password, which is the natural friction for destructive operations.

## When not to use run

`run` is not the right tool for things that have a structural alternative:

- **Creating files and directories.** Use `mkdir` and `file`.
- **Copying files.** Use `file from` or `copy into`.
- **Setting modes.** Use `mode` on `mkdir` or `file`.

Reach for `run` when you genuinely need a side effect that the language has no native form for: `git init`, `npm install`, `cargo new`, etc.

## See also

- @ref lang_stmt_include "include": run another spudplate template, with isolation and a per-template trust prompt.
- @ref lang_pitfalls "Pitfalls": shell-injection examples and defensive patterns.
- @ref lang_best_practices "Best practices": how to structure a template that uses `run`.
