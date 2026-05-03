# repeat {#lang_stmt_repeat}

```
repeat <int_var> as <iter> [when <condition>]
  ... statements ...
end
```

Runs the body block `<int_var>` times. The iterator `<iter>` is bound to the current iteration index (0-based, runs from `0` through `<int_var> - 1`). Nested `repeat`, `if`, `ask`, and any action statement may appear inside the body.

[TOC]

## Basic loop

```
ask num_modules "How many modules?" int default 0
repeat num_modules as i
  mkdir "module_{i}"
  file "module_{i}/README.md" content "# Module " + i
end
```

If `num_modules` is `3`, the loop runs three times, with `i` taking values `0`, `1`, `2`.

## when on repeat

The `when` clause skips the entire loop if the condition is false:

```
repeat num_weeks as week when use_weekly
  mkdir "week_{week}"
end
```

When `use_weekly` is `false`, the loop runs zero times, regardless of `num_weeks`.

## ask inside repeat

`ask` is allowed inside a `repeat` body. Each iteration prompts afresh and binds a fresh value for that iteration. The binding is gone at the start of the next iteration.

```
ask num_topics "How many topics?" int default 0
repeat num_topics as i
  ask topic "Topic {i}?" string
  mkdir "topics/{topic}"
end
```

Prompts inside `repeat` are indented two spaces per nesting level. The static `(N/M)` counter sits next to an `iteration K of L` indicator, for example `(3/7, iteration 2 of 4)`. Nested loops stack indicators outermost first.

## Scoping

A `repeat` body introduces a new scope. The iterator and any `let` or `as` declared inside the block are local to that body and are not visible after `end`.

```
repeat n as i
  let label = "step_" + i
  mkdir "step_{i}"
end
file label/"x" content ""    # error: 'label' is out of scope
```

The no-shadowing rule applies: an inner `let`, `as`, or repeat iterator cannot reuse a name visible in the surrounding scope. See @ref lang_scoping "Scoping" for the full rules.

## Reassignment in loops (accumulators)

A reassignment inside the body that targets a `let` from an outer scope mutates the outer binding. This is the way to accumulate a value across iterations.

```
let total = 0
let n = 5
repeat n as i
  total = total + i
end
file "sum.txt" content "Total: {total}"
```

The accumulator pattern is the only common reason to mutate a `let`; most templates run cleanly without reassignment.

## Nested loops

```
repeat num_weeks as week
  repeat num_days as day
    mkdir "week_{week}/day_{day}"
  end
end
```

Each loop has its own scope. The inner iterator `day` is not visible outside its loop, and cannot shadow `week`.

## See also

- @ref lang_stmt_ask "ask": prompts inside loops are valid and bind per-iteration.
- @ref lang_stmt_if "if": gate parts of the loop body without a `when` per statement.
- @ref lang_scoping "Scoping": full rules for repeat-body scope, shadowing, and accumulators.
