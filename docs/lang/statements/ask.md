# ask {#lang_stmt_ask}

```
ask <name> "<prompt>" <type> [options <v1> <v2> ...] [default <value>] [when <condition>]
```

Declares a variable `<name>` and prompts the user at run time. The answer is the value bound to `<name>` and is read-only for the rest of the run.

[TOC]

## Clauses

| Clause       | Effect                                                                              |
|--------------|-------------------------------------------------------------------------------------|
| `options`    | Restrict valid answers to a fixed list, presented as a numbered menu                |
| `default`    | Value used when the user submits an empty response                                  |
| `when`       | Skip the prompt entirely if the condition is false; the default is bound instead    |

Clause order is fixed: `options` first, then `default`, then `when`. The parser rejects other orderings.

## Types

| Type     | Accepted input                                                                                  |
|----------|--------------------------------------------------------------------------------------------------|
| `string` | Any text                                                                                         |
| `bool`   | `y`, `yes`, `true`, `n`, `no`, `false` (case-insensitive). The hint shows `[Y/n]` for `default true`, `[y/N]` for `default false`, and `[y/n]` otherwise |
| `int`    | Decimal integer                                                                                  |

The declared type is fixed and enforced for `default`, `options`, and any later use of the bound value.

## Required vs optional

A question is **required** unless it has a `default`. A required question with empty input re-prompts.

```
ask project_name "Project name?" string                  # required
ask license "License?" string default "MIT"              # optional, default "MIT"
```

## options

Restricts the answer to a fixed list. The prompt is rendered as a numbered menu, and the user can type either the literal value or the menu index.

```
ask format "Output format?" string options "pdf" "html" "latex"
ask postgres_version "Postgres version?" int options 15 16 17
```

When both `options` and `default` are present, the default value must equal one of the listed options. The parser rejects mismatches.

## default values

`default` accepts any expression of the matching type, including references to earlier variables and function calls.

```
ask slug "Slug?" string default lower(trim(project_name))
ask total "Total?" int default num_weeks * 7
```

Type mismatches between the declared type and a literal default are caught at parse time. Mismatches against an identifier or expression default are caught at validate time.

## when

Skips the prompt when the condition is false. A `when`-gated question **must** have a `default`. The default is bound when the gate is false, so later code always sees a real value.

```
ask use_tests "Tests?" bool default false
ask num_weeks "Weeks?" int default 0 when use_tests
```

If `use_tests` is `false`, `num_weeks` is bound to `0` without ever prompting.

## ask inside repeat

`ask` is allowed inside a `repeat` body. Each iteration prompts afresh and binds a fresh value scoped to that iteration. The binding is gone at the next iteration, so the same `ask` cannot bind two values at once.

```
repeat num_weeks as week
  ask topic "Topic for week {week}?" string
  mkdir "week_{week}/{topic}"
end
```

Prompts inside `repeat` are indented two spaces per nesting level. The static `(N/M)` counter sits next to an `iteration K of L` indicator, for example `(3/7, iteration 2 of 4)`.

The no-shadowing rule still applies: an `ask` inside the body cannot reuse a name visible from the surrounding scope.

## ask inside if

An `ask` inside an `if` body that has no inner `when` clause does not need a `default`: the `if` is the gate.

```
ask use_tests "Tests?" bool default false
if use_tests
  ask test_runner "Test runner?" string options "ctest" "gtest" default "ctest"
end
```

An `ask` inside `if` that **also** carries its own `when` clause still needs a `default`, because the inner gate can be false while the outer `if` is true.

## See also

- @ref lang_stmt_let "let": derive values without prompting.
- @ref lang_stmt_if "if": gate a whole block of `ask` statements on one condition.
- @ref lang_scoping "Scoping": full rules for `ask` inside `repeat` and `if`.
