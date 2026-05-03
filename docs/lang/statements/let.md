# let and reassignment {#lang_stmt_let}

```
let <name> = <expression>
<name> = <expression>
```

`let` declares a new variable. A bare `<name> = <expression>` line replaces the value of an existing `let` binding.

[TOC]

## let

Binds a fresh name to the value of an expression. The variable's type is inferred from the right-hand side and is fixed for the lifetime of the binding.

```
let slug = lower(trim(project_name))
let kebab = lower(replace(trim(name), " ", "-"))
let project_dir = slug + "-project"
let total_days = num_weeks * 7
```

`let` declarations cannot collide with any visible binding (`ask` answers, other `let` names, repeat iterators, path aliases). Shadowing is rejected; see @ref lang_scoping "Scoping".

A `let` value is purely a derivation. There is no prompt, no side effect, no filesystem activity.

## Reassignment

A bare `<name> = <expression>` line replaces the value of an existing `let` binding.

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
- Only `let` bindings are mutable. `ask` answers, path aliases (`as <name>` on a `mkdir` or `file`), and repeat iterators are read-only.
- Reassignment inside a `repeat` body that targets an outer `let` mutates the outer binding. This is the basis of the accumulator pattern.

## Statement-time evaluation

The interpreter captures values at the moment each statement runs. Two reads of the same variable across a reassignment see the new value:

```
let n = 1
mkdir "dir_{n}"           # creates dir_1
n = n + 1
mkdir "dir_{n}"           # creates dir_2
```

Each `mkdir` resolves its path before the next statement runs.

## When to use which

| If you want to...                                              | Use                                       |
|----------------------------------------------------------------|-------------------------------------------|
| Derive a value from earlier `ask` answers                       | `let`                                     |
| Build up a value across loop iterations                         | `let` outside the loop, reassign inside    |
| Give a name to a path you will reuse                            | `as` clause on `mkdir` or `file`           |
| Compute a default for an `ask`                                  | `default <expression>` on the `ask`        |

A `let` is preferable to inlining the same expression repeatedly: `let dir = ...` once, then refer to `dir` everywhere, gives a single point to change.

## See also

- @ref lang_stmt_ask "ask": bind a variable from user input rather than from an expression.
- @ref lang_paths "Path expressions": how a `let`-bound string can be used as a path root.
- @ref lang_scoping "Scoping": no-shadowing rule and reassignment-in-loops semantics.
