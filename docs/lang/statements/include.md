# include {#lang_stmt_include}

```
include <name> [when <condition>]
```

Runs another installed spudplate template by name, as an **independent subprocess**. The included template has its own questions, its own filesystem operations, and no shared scope with the caller.

[TOC]

## Subprocess semantics

`include` is not source-level inlining. The named template is invoked as a fresh `spudplate run`, with its own:

- Variable scope. Nothing the caller has bound is visible inside; nothing the includee binds leaks back out.
- Prompt sequence. The user answers the includee's questions in addition to the caller's.
- Filesystem flush. The includee writes its own files independently, in whichever directory it was designed to use.

This isolation makes templates composable: a `claude_setup` template can be included by both a Python project template and a Rust project template without either knowing how the other works.

## Resolving the name

The argument to `include` is a bare identifier (no quotes). It is matched against installed templates:

- When running a `.spud` file directly, the name is looked up in the install registry (`$SPUDPLATE_HOME`, `$XDG_DATA_HOME/spudplate`, or `~/.local/share/spudplate`). The named template must already be installed.
- When running an installed `.spp`, the bundler may have copied the dependency into the bundle's `_deps/` slot at export time, so the recipient does not need a separate install. (The current bundler does not yet do this; see `_deps` in @ref lang_reference "Language Reference".)

## when

The `when` clause skips the include if the condition is false. The conditional is evaluated at run time.

```
ask use_claude "Set up Claude config?" bool default false
include claude_setup when use_claude
```

If `use_claude` is `false`, the includee is not invoked and asks no questions.

## Why no source inlining

Source inlining was deliberately excluded. The reasons:

- **Author independence.** A team can ship `claude_setup` and a Python template independently; the Python template's stability does not depend on `claude_setup`'s source not changing.
- **Trust boundaries.** Each `.spud` runs as itself, so the user's "do you trust this template?" prompt is per-template.
- **Scoping clarity.** With no shared scope, there is no ambiguity about whose `let` or `ask` is whose.

If you want shared logic across templates, factor it into a small standalone template and `include` it.

## See also

- @ref lang_stmt_run "run": execute a shell command rather than a sub-template.
- @ref lang_pitfalls "Pitfalls": gotchas when composing templates via `include`.
