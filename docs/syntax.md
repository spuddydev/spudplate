# Spudlang Syntax Reference

`.spud` files are written in **spudlang**, a simple domain-specific language for template scaffolding. A `.spud` file describes a series of questions to ask the user and actions to take based on their answers.

---

## Overview

A `.spud` file executes top-to-bottom. Questions (`ask`) and actions (`mkdir`, `file`) can be freely interleaved. The output binary, however, collects **all user input first**, then performs all actions — so no prompts appear mid-file-creation.

---

## Statements

### `ask` — Prompt the user for input

```
ask <name> "<prompt>" <type> [required] [when <condition>]
```

Declares a variable `<name>` and asks the user a question at runtime.

- All questions are **optional by default**. If the user skips an optional question (empty input), any actions that reference that variable are silently skipped.
- Add `required` to force the user to provide a non-empty answer.
- Add `when <condition>` to only ask the question if the condition is true.

**Types:**

| Type     | Description                        |
|----------|------------------------------------|
| `string` | Arbitrary text input               |
| `bool`   | `true` / `false` (yes/no prompt)   |
| `int`    | Integer input                      |

**Examples:**

```
ask project_name "What is the project name?" string required
ask use_tests "Include a test suite?" bool
ask num_weeks "How many weeks is the project?" int when use_tests
```

---

### `let` — Declare a derived variable

```
let <name> = <expression>
```

Computes a new variable from an expression. Useful for transforming user input before using it in paths or file content.

**String operations:** `lower()`, `upper()`, `trim()`, `+` (concatenation)

**Integer operations:** `+`, `-`, `*`, `/`

**Examples:**

```
let slug = lower(trim(project_name))
let dir = slug + "-project"
let total_days = num_weeks * 7
```

---

### `mkdir` — Create a directory

```
mkdir <path> [from <source> [verbatim]] [mode <octal>] [when <condition>] [as <name>]
```

Creates a directory at the given path. Intermediate directories are created as needed. The path may contain `{variable}` interpolation.

- `from <source>` — creates the directory by copying the contents of the source directory. The source is embedded at compile time. `{var}` interpolation is applied at runtime unless `verbatim` is specified.
- `mode <octal>` — sets the directory's permissions (e.g., `mode 0755`).
- `when <condition>` — only creates the directory if the condition is true.
- `as <name>` — binds the path to an alias that later statements can reuse.

**Examples:**

```
mkdir "{dir}/src"
mkdir "{dir}/tests" when use_tests
mkdir "{dir}/private" mode 0700
mkdir templates from base_templates verbatim when use_templates as templatepath
```

---

### `copy` — Copy a directory into an existing destination

```
copy <source> into <destination> [verbatim] [when <condition>]
```

Copies the contents of `<source>` into an already existing `<destination>` directory. Unlike `mkdir ... from`, which creates the destination, `copy into` requires the destination to exist — use it when merging several sources into one directory.

- `verbatim` — suppress `{var}` interpolation when copying source file contents.
- `when <condition>` — only perform the copy if the condition is true.

**Examples:**

```
copy standard_templates into templatepath
copy philosophy_templates into templatepath when use_philosophy
copy assets into staticpath/assets verbatim
```

---

### `file` — Create or append to a file

```
file "<path>" [append] from "<source>" [verbatim] [mode <octal>] [when <condition>]
file "<path>" [append] content <expression>  [mode <octal>] [when <condition>]
```

Creates a file at `<path>`, either from a source file (`from`) or from an inline expression (`content`).

- `append` — appends to the file instead of overwriting. Without `append`, the file is created fresh (or overwritten if previously created in the same run).
- `from "<source>"` — embeds the contents of `<source>` at compile time. `{var}` interpolation is applied at runtime unless `verbatim` is specified.
- `content <expression>` — inline content; supports `{var}` interpolation via string concatenation.
- `mode <octal>` — sets file permissions (e.g., `mode 0644`, `mode 0755`).
- `when <condition>` — only creates the file if the condition is true.

**Examples:**

```
file "{dir}/README.md" content "# " + project_name
file "{dir}/.gitignore" from "templates/gitignore"
file "{dir}/run.sh" from "templates/run.sh" mode 0755
file "{dir}/main.tex" from "templates/main.tex" verbatim when format == "latex"
file "{dir}/log.txt" append content "Created by spudplate\n"
```

---

### `repeat` — Loop over a range

```
repeat <int_var> as <iter>
    # nested statements
end
```

Repeats the nested block `<int_var>` times. The loop variable `<iter>` holds the current iteration index (0-based). Nested `repeat` blocks and all action statements are allowed inside a `repeat`.

**Example:**

```
ask num_modules "How many modules?" int
repeat num_modules as i
    mkdir "{dir}/module_{i}"
    file "{dir}/module_{i}/README.md" content "# Module " + i
end
```

---

## Expressions

Expressions appear in `let` values, `content` values, and `when` conditions.

### Literals

```
"hello"      # string literal
42           # integer literal
true         # boolean literal
false        # boolean literal
```

### Variables

Any previously declared variable (from `ask` or `let`) can be referenced by name:

```
project_name
num_weeks
use_tests
```

### String functions

| Function     | Description                        |
|--------------|------------------------------------|
| `lower(x)`   | Converts string to lowercase       |
| `upper(x)`   | Converts string to uppercase       |
| `trim(x)`    | Strips leading/trailing whitespace |

### Operators

| Operator | Types         | Description              |
|----------|---------------|--------------------------|
| `+`      | string, int   | Concatenation / addition |
| `-`      | int           | Subtraction              |
| `*`      | int           | Multiplication           |
| `/`      | int           | Division                 |
| `==`     | any           | Equal                    |
| `!=`     | any           | Not equal                |
| `>`      | int           | Greater than             |
| `<`      | int           | Less than                |
| `>=`     | int           | Greater than or equal    |
| `<=`     | int           | Less than or equal       |
| `and`    | bool          | Logical AND              |
| `or`     | bool          | Logical OR               |
| `not`    | bool          | Logical NOT              |

---

## Variable Interpolation

`{var}` interpolation is available in:

- File and directory **paths** (`"src/{slug}/main.cpp"`)
- `content` expressions (via string concatenation)
- Contents of **`from` source files** (unless `verbatim` is used)

---

## Comments

Lines beginning with `#` are comments and are ignored by the compiler.

```
# This is a comment
ask name "Your name?" string
```

---

## Safety

The output binary will **refuse to write to any path that already existed before the run**. It tracks paths it creates during execution and only allows overwriting those. This tracking is ephemeral — no metadata is left behind after the binary exits.

---

## Full Example

```
ask project_name "Project name?" string required
ask use_tests "Include tests?" bool
ask num_weeks "Estimated weeks?" int when use_tests

let slug = lower(trim(project_name))
let dir = slug + "-project"

mkdir "{dir}"
mkdir "{dir}/src"
mkdir "{dir}/tests" when use_tests

file "{dir}/README.md" content "# " + project_name
file "{dir}/src/main.cpp" from "templates/main.cpp"
file "{dir}/tests/test_main.cpp" from "templates/test_main.cpp" when use_tests
```
