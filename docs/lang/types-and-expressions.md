# Types and Expressions {#lang_types}

Spudlang has three concrete types (`string`, `bool`, and `int`), declared on `ask` and inferred on `let`. Expressions show up in `let` values, `content` values, `default` clauses, `options` lists, `{expr}` interpolations, and `when` conditions. This page covers the type system, the expression grammar, and the rules for each context.

[TOC]

## Types

| Type     | Values                                                                       |
|----------|------------------------------------------------------------------------------|
| `string` | Any sequence of bytes inside `"..."`. The empty string is allowed.           |
| `bool`   | `true` or `false`.                                                           |
| `int`    | Whole numbers expressible as a sequence of decimal digits.                   |

There are no implicit conversions: a `string` value is never coerced to `int`, and an `int` is never coerced to `string`. The one place a value crosses type boundaries is `{expr}` interpolation, which stringifies its argument when splicing it into surrounding literal text.

Each `ask` statement declares its variable's type explicitly:

```
ask name "Project name?" string
ask use_tests "Tests?" bool default false
ask weeks "Weeks?" int default 0
```

`let` infers the type from the right-hand side and a `let` binding cannot change type. Reassignment to a `let` binding (`name = expr`) keeps the original type. Assigning a value of a different type is a validation error.

## Literals

| Literal kind | Examples                |
|--------------|--------------------------|
| `string`     | `""`, `"hello"`, `"with spaces"`, `"path/with/slashes"` |
| `int`        | `0`, `1`, `42`, `2026`   |
| `bool`       | `true`, `false`          |

A string literal that contains one or more `{...}` interpolations is parsed as a template-string expression, not a flat literal. See [Interpolation](#interpolation) below.

## Variable references

Any previously declared variable (from `ask` or `let`) can be referenced by name. The validator rejects references to undeclared names, references that are out of scope, and references whose type does not match the surrounding context (for example, an `int` variable used where a `bool` is expected).

```
ask project_name "Name?" string
let slug = lower(trim(project_name))   # project_name is in scope
let dir = slug + "-project"             # slug is in scope
```

## Operators

| Operator          | Operand types        | Result   | Description                               |
|-------------------|----------------------|----------|-------------------------------------------|
| `+`               | `int + int`          | `int`    | Addition                                  |
| `+`               | `string + string`    | `string` | Concatenation                             |
| `-`               | `int - int`          | `int`    | Subtraction                               |
| `*`               | `int * int`          | `int`    | Multiplication                            |
| `/`               | `int / int`          | `int`    | Integer division (truncates towards zero) |
| `==`, `!=`        | matching types       | `bool`   | Equality and inequality                   |
| `<`, `<=`, `>`, `>=` | `int` only        | `bool`   | Ordering                                  |
| `and`, `or`       | `bool and bool`      | `bool`   | Logical conjunction and disjunction       |
| `not`             | `bool`               | `bool`   | Logical negation                          |

There is no unary minus. To produce a negative value, subtract from zero or use a binary expression.

`+` is the only operator that works on both `string` and `int`. Mixing operand types (`"week " + n` where `n` is an `int`) is a validation error. Convert via interpolation first: `"week {n}"`.

Comparing values of different types with `==` or `!=` is also a validation error. Comparing `int` ordering operators (`<`, `<=`, `>`, `>=`) on non-`int` operands is rejected.

## Operator precedence

From lowest to highest:

| Level | Operators                                | Associativity |
|-------|------------------------------------------|---------------|
| 1     | `or`                                     | left          |
| 2     | `and`                                    | left          |
| 3     | `==`, `!=`, `<`, `<=`, `>`, `>=`         | left          |
| 4     | `+`, `-`                                 | left          |
| 5     | `*`, `/`                                 | left          |
| 6     | `not`                                    | right         |
| 7     | function calls, literals, identifiers, `(...)` | n/a       |

Parentheses can override the default order:

```
when (use_tests or use_docs) and not draft
```

## Built-in functions

Four string functions are built in. There is no facility for user-defined functions.

| Call                  | Returns | Description                                          |
|-----------------------|---------|------------------------------------------------------|
| `lower(s)`            | string  | ASCII lowercase                                      |
| `upper(s)`            | string  | ASCII uppercase                                      |
| `trim(s)`             | string  | Strip leading and trailing whitespace                |
| `replace(s, from, to)`| string  | Replace every occurrence of `from` in `s` with `to`. Empty `from` is a runtime error |

Arity is checked at parse time. Passing the wrong number of arguments is a parse error; passing the wrong type is a validation error.

```
let slug = lower(trim(project_name))
let kebab = lower(replace(trim(name), " ", "-"))
```

Function calls compose freely; the result of one call can be the argument of another.

## Interpolation {#interpolation}

`{expr}` interpolation splices the value of an expression into surrounding literal text. It applies in three contexts:

### 1. String literals

Inside a `"..."` literal, every `{...}` segment is parsed as an expression in the spudlang grammar. The expression is evaluated, stringified, and concatenated with the surrounding literal pieces.

```
let label = "v" + version
file "build.txt" content "name={name}, build={label}"
```

The full expression grammar applies inside the braces, including identifiers, arithmetic, function calls, and string concatenation:

```
content "slug={lower(trim(name))}"
content "n+1={n + 1}"
```

Brace pairs are balanced, so `"{f({x})}"` parses as a single interpolation that calls `f` with `x`. A literal double-quote inside an interpolation closes the surrounding string literal, so keep nested expressions to identifiers and operations on them, or precompute the string with a `let`.

An unclosed `{`, an empty `{}`, or content that does not parse as an expression is reported at parse time. An interpolation that references an undeclared name fails at validation time.

### 2. Path expressions

Inside a quoted path segment, `{expr}` works exactly as in a string literal. The same expression grammar applies.

```
mkdir "week_{n}"
file "src/{slug}/main.cpp" content ""
```

`{...}` outside a quoted segment in a path is a parse error: bare-identifier path segments must be plain identifiers (variable references), and there is no syntax for inline interpolation in an unquoted segment. See @ref lang_paths "Path expressions" for the full path grammar.

### 3. `from` source files and `copy` source files

When a `file ... from <source>` or `copy <source> into <dest>` reads a file from the bundle, the file's contents are scanned for bare `{ident}` substitutions. **Only bare identifier substitution applies.** Function calls, arithmetic, and string concatenation inside the braces are not supported in source-file contents. For full expression power, use `content <expr>` instead of `from`.

To copy bytes verbatim and skip the scan entirely, add the `verbatim` keyword:

```
file "main.cpp" from "templates/main.cpp"           # {ident} substituted
file "main.cpp" from "templates/main.cpp" verbatim   # bytes copied as-is
```

A literal `{` or `}` in a non-`verbatim` source file is a runtime error. Switch to `verbatim` if the file genuinely needs braces.

The interpreter auto-detects binary content: a source file whose bytes do not form valid UTF-8 is treated as `verbatim` regardless of the keyword, so binary assets such as PNGs or favicons just work.

## Where expressions appear

| Context            | Expression must be of type      | Notes                                                |
|--------------------|----------------------------------|------------------------------------------------------|
| `let <name> = E`   | any                             | `<name>` takes the type of `E`                       |
| `<name> = E`       | the type of the existing binding | Reassignment to the same type only                   |
| `ask ... default E`| matching the declared `ask` type | Literal-default mismatches caught at parse time      |
| `ask ... options E1 E2 ...` | matching the declared `ask` type | All option literals must match the type     |
| `when E`           | `bool`                          | Used by `mkdir`, `file`, `copy`, `repeat`, `ask`, `include`, `run` |
| `if E`             | `bool`                          | Same as `when`                                       |
| `file "..." content E` | `string` (after stringification) | Non-string `E` is rejected; pre-stringify with interpolation |
| `run E`            | `string`                        | The evaluated string is the shell command            |
| `run ... timeout E`| positive `int`                  | Per-statement timeout in seconds                     |
| `{E}` interpolation| any (stringified)                | `bool` becomes `true` or `false`; `int` is decimal   |
