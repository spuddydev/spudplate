# Scoping and Conditions {#lang_scoping}

Spudlang has lexical scoping with one strict rule: a name may not be silently shadowed. Path aliases bound under a `when` clause are conditionally scoped, and the validator normalises bool conditions before comparing them. This page covers the scoping model, the no-shadowing rule, and the normalisation logic.

[TOC]

## Scopes

A spudlang program has one program-level scope and one nested scope per `repeat` or `if` body. Scopes nest: an inner scope can see everything in the outer scope (subject to the conditional-alias rule below), but not the other way around.

The bindings that occupy a scope:

| Binding kind         | Created by                                          | Mutable |
|----------------------|-----------------------------------------------------|---------|
| `ask` answer         | `ask <name>`                                        | no      |
| `let` value          | `let <name> = ...`                                  | yes (via reassignment) |
| Path alias           | `as <name>` clause on `mkdir` or `file`             | no      |
| Repeat iterator      | `repeat <int_var> as <iter>`                        | no      |

Each binding lives in the scope where it was declared. A binding inside a `repeat` body is visible only inside that body. After `end`, the body's scope is popped and its bindings are unreachable.

```
let outer_count = 5
repeat outer_count as i
  let label = "step_" + i
  mkdir "step_{i}"           # ok
end
file label/"x" content ""    # error: 'label' is out of scope here
```

## No-shadowing rule

A `let` or `as` binding inside a nested scope **cannot** reuse a name that is already visible from any outer scope. The same rule applies to repeat iterators.

```
let i = 0
repeat n as i               # error: shadowing of visible name 'i'
  ...
end
```

Shadowing rejection covers all kinds of binding: a `let` cannot shadow an `ask`, an `ask` cannot shadow a path alias, a repeat iterator cannot shadow anything in its enclosing scope. The language never silently shadows.

`ask` inside a `repeat` body must also pass the no-shadowing check: each iteration produces a fresh binding scoped to that iteration, but the chosen name still cannot collide with anything visible from outside the loop.

## Conditional alias scoping

A path alias bound by `mkdir <path> as <name> when <cond>` or `file <path> as <name> when <cond>` is **conditional**. References to that alias outside a statement guarded by an equivalent condition are rejected at validate time.

```
mkdir "tests" as tests_path when use_tests
file tests_path/"main.cpp" from "templates/test.cpp" when use_tests   # ok, same condition
file tests_path/"README.md" content "# Tests"                          # error: missing matching when
```

Without the rule, a template could compile but crash at run time when the alias was used while its bind statement was skipped. The rule moves that mistake to validation.

If the binding statement has no `when`, the alias is unconditional and can be used anywhere.

## Condition normalisation

The validator normalises `when` expressions before comparing them. Two normalised expressions are equivalent if their abstract syntax trees match exactly.

The normalisations the validator performs:

| Source                        | Normal form                          |
|-------------------------------|--------------------------------------|
| `b` (where `b` is a `bool`)   | `b == true`                          |
| `not b`                       | `b == false`, equivalent to `b != true` |
| `not not b`                   | `b == true`                          |
| `b == false`                  | matches `not b` form                 |
| `n > 4` (non-bool)            | structural, no simplification        |
| `format == "latex"`           | structural, no simplification        |
| `a and b`                     | structural, **not** equivalent to `b and a` |

The bool variable's type is required for the bool-specific simplifications, so the validator builds a type map from `ask` declarations as it walks the program. This is also why `let`-typed variables behave the same way once their inferred type is known.

### Known limitation: commutativity

`a and b` and `b and a` are not considered equivalent, even though they evaluate identically. The same is true of `or`. If you bind an alias under a compound condition, repeat the operands in the same order on every reference:

```
mkdir "x" as x when use_a and use_b
file x/"y" content ""        when use_a and use_b   # ok
file x/"z" content ""        when use_b and use_a   # error: not recognised as equivalent
```

## Type tracking and type errors

Beyond scoping, the validator enforces type rules:

- An expression in a `bool`-only context (`when`, `if`, `not`, `and`, `or`) must be `bool`.
- An expression in an arithmetic context must be `int`.
- Both sides of `==` and `!=` must have the same type.
- Ordering operators (`<`, `<=`, `>`, `>=`) require both sides to be `int`.
- A `default` value must match the declared `ask` type.
- Each `options` entry must match the declared `ask` type.
- A reassignment must match the original binding's type.
- A function argument's type must match the function (the four built-ins are all `string`-only).

Type errors are surfaced before any prompt runs, so a template either passes validation or stops before asking the user anything.

## Mutation and capture

The interpreter captures values **at statement time**, not at name-binding time. After

```
let n = 1
mkdir "dir_{n}"
n = n + 1
mkdir "dir_{n}"
```

you get `dir_1` and `dir_2`. Each `mkdir` resolves its path before the next statement runs, so reassignment between two reads of `n` is safe.

Reassignments inside a `repeat` body that target a `let` binding from an outer scope mutate the outer binding (the basis of the accumulator pattern). Bindings local to a `repeat` body cannot be reassigned from outside the body, since they are out of scope by the time the loop ends.
