# Lexical Structure {#lang_lexical}

Spudlang source is a UTF-8 text file made of one logical statement per line. The lexer turns the text into a stream of tokens that the parser then assembles. This page describes that token-level surface: what counts as whitespace, what an identifier looks like, what a literal accepts, and how comments and continuations work.

[TOC]

## Source encoding

`.spud` files are UTF-8. The lexer is byte-oriented and does not normalise or transliterate; non-ASCII characters appear inside string literals exactly as written.

## Lines and whitespace

A spudlang program is line-oriented. Each statement occupies one logical line; the parser expects a newline (or end-of-file) between statements.

Within a line, **spaces and tabs are interchangeable and not significant** beyond separating tokens. The lexer collapses runs of horizontal whitespace before reading the next token. Vertical whitespace (an empty line) is allowed anywhere a newline would be, and is treated the same as a single newline.

```
ask    name    "Your name?"    string
ask	name	"Your name?"	string   # tabs work the same
```

## Line continuation

A backslash `\` immediately before a newline continues the current logical line on the next physical line. The parser sees a single statement.

```
ask format "What output format?" string \
    options "pdf" "html" "latex" \
    default "pdf" \
    when use_docs
```

The lexer discards the `\`, any trailing horizontal whitespace before the newline, the newline itself, and any leading whitespace on the next line.

A `\` that is not immediately followed by a newline (allowing only spaces and tabs in between) is **not** a continuation and produces a lexer error at the offending position.

## Comments

Lines beginning with `#`, or a `#` after any token on a line, start a comment that runs to the end of the line.

```
# Standalone comment
ask name "Your name?" string   # trailing comment
```

There is no block-comment form.

## Identifiers

An identifier starts with an ASCII letter or underscore and continues with any number of letters, digits, or underscores.

```
project_name
slug
_internal
n
total_days
```

Identifiers are case-sensitive. `Name` and `name` are different identifiers. The convention used throughout this reference, the test suite, and the example library is **`snake_case`**.

## Keywords

The following words are reserved and cannot be used as identifiers:

| Group       | Words                                                                                              |
|-------------|----------------------------------------------------------------------------------------------------|
| Statements  | `ask`, `let`, `mkdir`, `file`, `copy`, `repeat`, `if`, `end`, `include`, `run`                     |
| Clauses     | `from`, `into`, `content`, `default`, `options`, `when`, `verbatim`, `append`, `mode`, `as`, `in`, `timeout` |
| Types       | `string`, `bool`, `int`                                                                            |
| Logical     | `and`, `or`, `not`                                                                                 |
| Boolean     | `true`, `false`                                                                                    |

Reserved words also cannot appear as bare path segments — see [Path expressions](paths.md) and [Pitfalls](pitfalls.md) for the practical consequences.

## Literals

### String

A string literal is a run of characters enclosed in double quotes.

```
"hello"
"with spaces"
"path/with/slashes"
```

Strings have no escape sequences. A backslash is a literal byte; `"\n"` is two characters (a backslash and the letter `n`), not a newline. To write a literal newline, embed an actual line break inside the string:

```
file "notes.txt" content "first line
second line
"
```

A double quote cannot appear inside a string. There is no `\"` escape. Use `{expr}` interpolation if you need to compose a string that contains a quote (for example, by binding the quote to an `ask` value or another `let`).

Strings may contain `{expr}` interpolations. See [Types and expressions](types-and-expressions.md) for the rules.

### Integer

An integer literal is one or more decimal digits.

```
0
42
2026
```

There is no unary minus. A literal `-1` does not parse — write the value as a binary subtraction such as `0 - 1`, or bind a positive integer and subtract it.

There is no support for hexadecimal, octal (with the exception of `mode 0755` octal forms — see [Statements](statements.md)), or floating-point literals.

### Boolean

`true` and `false` are keyword literals.

```
ask use_tests "Tests?" bool default false
let ready = true
```

## Punctuation

| Token | Used in                                                                       |
|-------|-------------------------------------------------------------------------------|
| `+`, `-`, `*`, `/` | Arithmetic and string concatenation (`+`)                          |
| `==`, `!=`, `<`, `<=`, `>`, `>=` | Comparison operators                                 |
| `=`   | Assignment in `let` and reassignment statements                               |
| `(`, `)` | Grouping in expressions; argument lists for `lower`, `upper`, `trim`, `replace` |
| `{`, `}` | Open and close interpolation in path expressions                            |
| `,`   | Argument separator in function calls                                          |
| `.`   | Path-segment separator (e.g. `README.md` between two literal segments)        |
| `/`   | Path-segment separator                                                        |

Punctuation tokens that are not used in a given context produce a parse error rather than a lexer error.

## Newlines and statement termination

Every statement is terminated by a newline. A line that contains only whitespace and a comment is treated as an empty line and is skipped by the parser. The end of the file acts as an implicit newline after the last statement, so a trailing newline is optional but not harmful.

Inside `repeat` and `if` bodies, newlines separate statements just as at the top level; the body ends when the parser sees the `end` keyword as the first non-whitespace token on a line.
