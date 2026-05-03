# include {#lang_stmt_include}

```
include <name> [when <condition>]
```

Runs another installed spudplate template by name, **inline at the include point**. The included template has isolated variable scope, but its prompts and filesystem operations interleave with the caller's in source order.

[TOC]

## In-place semantics

`include` is not source-level inlining and it is not a subprocess. It runs the named template as a child of the current run, at the position where the `include` statement appears, with:

- **Isolated variable scope.** Nothing the caller has bound is visible inside; nothing the includee binds leaks back out.
- **In-place prompts.** Statements run top to bottom across the parent and the includee, so the user answers the includee's questions at exactly the point the `include` appears.
- **Shared filesystem flush.** The includee's `mkdir`, `file`, and `copy` operations join the parent's deferred queue. The whole run either commits or makes no changes.

The result is composable templates: a `claude_setup` template can be included by both a Python project template and a Rust project template without either knowing how the other works.

```
ask use_claude "Set up Claude config?" bool default false
include claude_setup when use_claude
ask name "Project name?" string
```

The user is prompted for `use_claude`, then (if true) for whatever questions `claude_setup` defines, then for `name`.

## Resolving the name

The argument to `include` is a bare identifier (no quotes). At install time, the named template must already be installed under the install root (`$SPUDPLATE_HOME`, `$XDG_DATA_HOME/spudplate`, or `~/.local/share/spudplate`). The bundler reads the bytes of `<install-root>/<name>.spp` and embeds them inside the parent spudpack as a dependency. At run time the parent reads the dep from its own bundle, so the recipient does not need the dep separately installed.

## when

The `when` clause skips the include if the condition is false. The conditional is evaluated at run time.

```
ask use_claude "Set up Claude config?" bool default false
include claude_setup when use_claude
```

If `use_claude` is `false`, the includee is not invoked and asks no questions.

## Limits

- Nested includes are capped at sixteen levels deep. Hitting the cap is almost always a self-include cycle that snuck past install-time checks; raise it only with care.
- A dep that fails to decode, deserialise, or validate at run time produces a runtime error tagged with the include site.

## Why no source inlining

Source inlining was deliberately excluded. The reasons:

- **Author independence.** A team can ship `claude_setup` and a Python template independently; the Python template's stability does not depend on `claude_setup`'s source not changing.
- **Trust boundaries.** Each `.spud` is validated and its `run` clauses are surfaced for explicit consent; isolated scope keeps each template's questions and bindings clearly its own.
- **Scoping clarity.** With no shared scope, there is no ambiguity about whose `let` or `ask` is whose.

If you want shared logic across templates, factor it into a small standalone template and `include` it.

## See also

- @ref lang_stmt_run "run": execute a shell command rather than a sub-template.
- @ref lang_pitfalls "Pitfalls": gotchas when composing templates via `include`.
