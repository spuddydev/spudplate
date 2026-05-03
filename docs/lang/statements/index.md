# Statements {#lang_statements}

A spudlang program is a sequence of statements that the interpreter executes top to bottom. There are nine statement kinds. Each has its own page covering its full grammar, examples, and edge cases. This page is the cheatsheet and the orientation guide.

[TOC]

## Quick reference

| Statement                  | One-line summary                                                       |
|----------------------------|------------------------------------------------------------------------|
| @subpage lang_stmt_ask     | Prompt the user and bind their answer to a variable                    |
| @subpage lang_stmt_let     | Declare or reassign a variable bound to an expression                  |
| @subpage lang_stmt_mkdir   | Create a directory, optionally populated from a source tree            |
| @subpage lang_stmt_file    | Create or append to a file from inline text or a source file           |
| @subpage lang_stmt_copy    | Merge a source directory into an existing destination                  |
| @subpage lang_stmt_repeat  | Loop a block N times, with a per-iteration index                       |
| @subpage lang_stmt_if      | Run a block when a condition is true                                   |
| @subpage lang_stmt_include | Run another bundled template inline with isolated variable scope        |
| @subpage lang_stmt_run     | Execute a shell command after explicit user authorisation              |

## What should I use?

The decision usually comes down to "what kind of thing am I trying to do?".

### Asking the user

Use `ask`. Add `default` if the answer is optional, `options` if you want to bound it to a fixed list, and `when` if the question only matters under some other condition (a `when`-gated `ask` requires a `default`).

### Creating a directory

| If you want to...                                         | Use                                       |
|-----------------------------------------------------------|-------------------------------------------|
| Create an empty directory                                 | `mkdir`                                   |
| Create a directory and copy a source tree into it         | `mkdir from`                              |
| Add files into a directory you already created            | `copy into`                               |

`mkdir from` and `copy into` look similar but solve different problems. `mkdir from` makes a new directory and populates it. `copy into` errors if its destination does not already exist; it is the right tool for merging multiple sources into one directory.

### Creating a file

| If you want to...                                         | Use                                       |
|-----------------------------------------------------------|-------------------------------------------|
| Write a file from a string expression                     | `file content`                            |
| Copy a file from the bundle, with `{ident}` substitution  | `file from`                               |
| Copy a file with no substitution at all                   | `file from ... verbatim`                  |
| Append to a file you created earlier in the same run      | `file append`                             |

### Branching

| If you want to...                                              | Use                                       |
|----------------------------------------------------------------|-------------------------------------------|
| Gate a single statement on a condition                          | `when` clause on the statement            |
| Gate a block of statements on a condition                       | `if` block                                |
| Gate everything inside a loop on a condition                    | `when` clause on `repeat`                 |

### Repeating actions

Use `repeat` with an `int` count. The iterator is local to the loop body. `ask` inside `repeat` runs once per iteration and binds a fresh value each time.

### Running other code

| If you want to...                                              | Use                                       |
|----------------------------------------------------------------|-------------------------------------------|
| Run another bundled template inline at this point               | `include`                                 |
| Run a shell command after the user authorises                   | `run`                                     |

## Execution model

Filesystem actions (`mkdir`, `file`, `copy`) are queued during execution and flushed at the end of the run. Internal state (variables, loop counters, `if` and `when` evaluations) runs throughout, so a `let` based on an `ask` answer is bound the moment the answer is received, not at flush time.

`include` runs the bundled child template inline at the include point, so its prompts interleave with the parent's in source order; the child's filesystem operations join the parent's deferred queue. `run` executes during the flush. A failed `run` aborts the rest of the flush; operations queued before the failure remain on disk.

If the user aborts at any prompt, no flush happens and the filesystem is untouched.

## Common idioms

A few patterns recur across most templates and are covered in @ref lang_best_practices "Best practices":

- The **alias-then-append** idiom: `file ... as readme; file readme append content "..." when use_X` for sectioned README files.
- The **slug-and-dir** idiom: an early `let dir = lower(slug) + "-project"` makes one path root that every subsequent statement reuses.
- The **bool-question gate**: `ask use_tests bool default false` followed by `if use_tests ... end` keeps optional sections together.
- The **`mkdir from` then `copy into`** idiom: create a base directory from one template tree, then merge optional add-ons in.

## See also

- @ref lang_paths "Path expressions": the path grammar every statement shares.
- @ref lang_scoping "Scoping and conditions": how `when` clauses, `if`, and `repeat` interact with variable scope.
- @ref lang_pitfalls "Pitfalls": known footguns when writing statements.
