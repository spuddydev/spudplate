# if {#lang_stmt_if}

```
if <condition>
  ... statements ...
end
```

Runs the body block when `<condition>` evaluates to `true`. The condition uses the same expression grammar as `when`. Nested `if` and `repeat`, `ask`, and any action statement may appear inside.

[TOC]

## When to use if vs when

`if` and the per-statement `when` clause solve the same problem from opposite ends:

- `when` gates a single statement.
- `if` gates a block of statements with one shared condition.

If you find yourself writing the same `when <cond>` on three or more consecutive statements, `if` is usually clearer:

```
if use_tests
  mkdir "tests"
  file "tests/README.md" content "# Tests"
  file "tests/test_main.cpp" from "templates/test_main.cpp"
end
```

versus

```
mkdir "tests" when use_tests
file "tests/README.md" content "# Tests" when use_tests
file "tests/test_main.cpp" from "templates/test_main.cpp" when use_tests
```

Both are valid. The `if` form makes the relationship between the statements visible.

## No else, no else if

Spudlang has no `else` or `else if`. A multi-branch decision composes from nested `if` blocks or from per-statement `when` clauses with mutually-exclusive conditions.

```
ask language "Language?" string options "python" "rust" "go"

if language == "python"
  copy "templates/python" into project
end
if language == "rust"
  copy "templates/rust" into project
end
if language == "go"
  copy "templates/go" into project
end
```

For two-branch decisions, two `if` blocks with negated conditions work, but a `when` on each statement is often more readable.

## No when on if

An `if` block does **not** itself accept a `when` clause: the `if` is the gate. To express "do this block only when `a and b`", put the compound condition on the `if`:

```
if use_tests and use_coverage
  mkdir "coverage"
end
```

Inner statements may still carry their own `when`, which combines with the outer `if`:

```
if use_tests
  mkdir "tests"
  file "tests/coverage.cfg" content "..." when use_coverage
end
```

## ask inside if

An `ask` lexically inside an `if` body that has no inner `when` clause does **not** need a `default`. The `if` is the gate.

```
ask use_tests "Tests?" bool default false
if use_tests
  ask test_runner "Test runner?" string options "ctest" "gtest" default "ctest"
end
```

If `use_tests` is `false`, the inner `ask` never prompts. The variable `test_runner` is **not** bound in that case.

An `ask` inside `if` that **also** carries its own `when` clause still needs a `default`, because the inner gate can be false while the outer `if` is true.

## Scoping

An `if` body introduces a new scope. `let` and `as` declared inside the block are local to it and are not visible after `end`. The no-shadowing rule applies.

```
ask use_tests "Tests?" bool default false
if use_tests
  let test_dir = "{dir}/tests"
  mkdir test_dir
end
file test_dir/"main.cpp" content ""    # error: 'test_dir' is out of scope
```

A path alias bound by `as` inside an `if` is conditionally scoped under the `if`'s condition.

## See also

- @ref lang_stmt_repeat "repeat": loop a block; combine with `if` or `when` to gate inside the loop.
- @ref lang_scoping "Scoping": rules for `let`, `as`, and `ask` inside an `if`.
- @ref lang_types "Types and expressions": full grammar for `<condition>`.
