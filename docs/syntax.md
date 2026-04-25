# Spudlang Syntax Reference

`.spud` files are written in **spudlang**, a simple domain-specific language for template scaffolding. A `.spud` file describes a series of questions to ask the user and actions to take based on their answers.

---

## Overview

A `.spud` file executes top-to-bottom. Questions (`ask`) and actions (`mkdir`, `file`, `copy`, `include`) can be freely interleaved.

The interpreter runs the file in a single pass. Internal state (variables, loops, conditions) evaluates as it is encountered, but filesystem writes are **deferred until the end of the run**. This guarantees the template either fully succeeds or makes no changes — you never see half-written output if the user aborts a prompt.

---

## Statements

### `ask` — Prompt the user for input

```
ask <name> "<prompt>" <type> [options <v1> <v2> ...] [default <value>] [when <condition>]
```

Declares a variable `<name>` and asks the user a question at runtime.

- A question is **required** unless it has a `default` value. With a `default`, the user may skip the prompt (empty input) and the default value is used in its place.
- The `default` accepts any expression — a literal, a previously declared variable, or a computed expression like `lower(trim(project_name))`. The expression is evaluated against the live environment when the user skips the prompt.
- `options` restricts valid answers to a fixed set, presented as a numbered menu. The user may type either the literal value or its 1-based number.
- `when <condition>` only asks the question if the condition is true.
- `ask` is allowed inside `repeat`. Each iteration prompts afresh; the binding lives only for that iteration. Prompts inside `repeat` are indented by 2 spaces per nesting level and are not counted in the `(N/M)` progress indicator (since the iteration count is dynamic). Shadowing rules still apply — an `ask` cannot reuse a name visible from the surrounding scope.

**Types:**

| Type     | Description                                              |
|----------|----------------------------------------------------------|
| `string` | Arbitrary text input                                     |
| `bool`   | Accepts `y`, `yes`, `true` and `n`, `no`, `false` (case-insensitive). The hint shows `[Y/n]` for `default true`, `[y/N]` for `default false`, `[y/n]` otherwise |
| `int`    | Integer input                                            |

**Examples:**

```
ask project_name "What is the project name?" string
ask use_tests "Include a test suite?" bool default false
ask license "License?" string default "MIT"
ask num_weeks "How many weeks?" int when use_tests
ask format "Output format?" string options "pdf" "html" "latex" default "pdf"
ask postgres_version "Postgres version?" int options 15 16 17

ask project_name "Project name?" string
ask slug "Slug?" string default lower(trim(project_name))
```

---

### `let` — Declare a derived variable

```
let <name> = <expression>
```

Computes a new variable from an expression. Useful for transforming user input before using it in paths or file content.

**String operations:** `lower()`, `upper()`, `trim()`, `replace()`, `+` (concatenation)

**Integer operations:** `+`, `-`, `*`, `/`

**Examples:**

```
let slug = lower(trim(project_name))
let kebab = lower(replace(trim(project_name), " ", "-"))
let dir = slug + "-project"
let total_days = num_weeks * 7
```

---

### Reassignment — Update an existing variable

```
<name> = <expression>
```

A bare `name = expr` line replaces the value of an existing `let` binding. Use this for incremental updates or loop accumulators:

```
let total = 0
let n = 3
repeat n as i
  total = total + i
end
```

**Rules:**

- The name must already be declared by `let` and visible in the current scope. Use `let` for the first binding; reassignment is rejected for undeclared names.
- Only `let` bindings are mutable. `ask` answers, path aliases (`as <name>` on `mkdir` or `file`), and repeat iterators are read-only — assigning to them is a validation error.
- The runtime captures values at statement time. After `let n = 1`, `mkdir dir_{n}`, `n = n + 1`, `mkdir dir_{n}` you get two directories `dir_1` and `dir_2` — each `mkdir` resolves its path before the next statement runs, so reassignment between them is safe.

Reassignment to an outer-scope `let` from inside a `repeat` body mutates the outer binding (used for accumulators). Bindings local to a `repeat` body cannot be reassigned from outside it once the loop ends — they are out of scope.

---

### Path expressions

Paths in `mkdir`, `file`, and `copy` are written as **path expressions** — an unquoted sequence of identifiers, slashes, dots, hyphens, and `{expr}` interpolations.

```
mkdir static/notes
file static/notes/README.md content ""
mkdir week_{n}
mkdir my-project/pre-commit-hooks
file src/main.cpp from base/main.cpp
```

Hyphens are allowed inside a path but cannot start one — `mkdir -foo` would be ambiguous with the subtraction operator and is rejected. Paths containing spaces must be quoted: `mkdir "my notes"`.

`{expr}` interpolation works directly in unquoted paths: `mkdir week_{n}`, `file {prefix}/README.md`.

`mkdir` creates intermediate directories automatically (`mkdir -p` semantics).

#### `as <varname>` — bind a path alias

The `as <varname>` clause binds the path to a variable for reuse. Any identifier in a subsequent path expression is checked against bound aliases — if it matches, it is a variable reference; if not, it is a literal path component. This applies to every segment, not just the leading one:

```
mkdir static as staticpath
mkdir notes as notespath
mkdir staticpath/notespath/week_{n}    # both staticpath and notespath are alias references
```

On `file`, `as` binds the file path — primarily useful for conditional appends (see `file` below).

If a path segment matches a bound alias but you want the literal directory name instead, quote that segment: `mkdir "staticpath"/notes`.

##### Conditional alias scoping

`as <varname>` on a conditional `mkdir` or `file` is a compile-time error if the alias is referenced outside a statement with a semantically equivalent `when` condition. Conditions are compared after normalization, so `use_git`, `use_git == true`, and `not not use_git` are all considered equivalent. Commutativity (`a and b` vs `b and a`) is **not** considered equivalent — a known limitation.

If the binding is unconditional, it can be referenced anywhere.

---

### `mkdir` — Create a directory

```
mkdir <path> [from <source>] [verbatim] [mode <octal>] [when <condition>] [as <name>]
```

Creates a directory at the given path expression.

- `from <source>` — populates the new directory from the contents of `<source>`. The source is embedded at compile time. `{var}` interpolation is applied at runtime to copied file contents unless `verbatim` is specified.
- `mode <octal>` — sets directory permissions (e.g., `mode 0755`).
- `when <condition>` — only creates the directory if the condition is true.
- `as <name>` — binds the path to an alias for later reuse.

**Examples:**

```
mkdir {dir}/src
mkdir {dir}/tests when use_tests
mkdir {dir}/private mode 0700
mkdir templates from base_templates verbatim when use_templates as templatepath
```

---

### `copy` — Copy a directory into an existing destination

```
copy <source> into <destination> [verbatim] [when <condition>]
```

Copies the contents of `<source>` into an **already existing** `<destination>` directory. Errors if the destination does not exist — use `mkdir from` for that. Use `copy` to merge several sources into one directory.

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
file <path> [append] from <source> [verbatim] [mode <octal>] [when <condition>] [as <name>]
file <path> [append] content <expression>     [mode <octal>] [when <condition>] [as <name>]
```

Creates a file at `<path>`, either from a source file (`from`) or from an inline expression (`content`).

- `append` — appends to the file instead of overwriting. Without `append`, the file is created fresh (or overwritten if previously created in the same run).
- `from <source>` — embeds the contents of `<source>` at compile time. `{var}` interpolation is applied at runtime unless `verbatim` is specified.
- `content <expression>` — inline content. The resulting string is interpolated: any `{var}` occurrence in the value is replaced with the bound variable's value (`content "n={n}"` writes `n=1` when `n` is `1`). String concatenation also works (`content "n=" + name`). An unclosed `{` or an unknown variable name is a runtime error.
- `mode <octal>` — sets file permissions (e.g., `mode 0644`, `mode 0755`).
- `when <condition>` — only creates the file if the condition is true.
- `as <name>` — binds the file path; useful for conditional appends.

**Examples:**

```
file {dir}/README.md content "# " + project_name
file {dir}/.gitignore from templates/gitignore
file {dir}/run.sh from templates/run.sh mode 0755
file {dir}/main.tex from templates/main.tex verbatim when format == "latex"
file {dir}/log.txt append content "Created by spudplate\n"
```

Conditional append via alias:

```
file {dir}/README.md content "# " + project_name as readme
file readme append content "## Notes\n" when use_notes
file readme append content "## Testing\n" when use_tests
```

---

### `repeat` — Loop over a range

```
repeat <int_var> as <iter> [when <condition>]
    # nested statements
end
```

Repeats the nested block `<int_var>` times. The loop variable `<iter>` holds the current iteration index (0-based). Nested `repeat` blocks, `ask`, and all action statements are allowed inside the body.

The optional `when` clause skips the entire loop if the condition is false.

#### Repeat scoping

A `repeat` block introduces a new scope. The iterator variable and any `let` or `as` names declared inside the block are **local to that loop body** — references from outside are rejected.

A `let` or `as` binding inside the block whose name collides with any currently visible outer binding (or with the iterator of any enclosing repeat) is rejected as a **shadowing error**. The language never silently shadows.

**Example:**

```
ask num_modules "How many modules?" int default 0
repeat num_modules as i when num_modules > 0
    mkdir {dir}/module_{i}
    file {dir}/module_{i}/README.md content "# Module " + i
end
```

---

### `include` — Run another installed template

```
include <name> [when <condition>]
```

Runs another installed spudplate template by name as an **independent subprocess** — its own questions, its own file creation, no shared scope. The named template must be installed (or bundled into `_deps/` at export time).

- `when <condition>` — only run the include if the condition is true.

```
ask use_claude "Set up Claude config?" bool default false
include claude_setup when use_claude
```

There is no source-level inlining; all template reuse goes through `include` and the installed registry.

---

### `run` — Execute a shell command

```
run <expression> [in <path>] [when <condition>]
```

Runs a shell command via `/bin/sh -c`. The expression evaluates to a string at runtime — supporting concatenation with `ask`/`let` values — and the resulting command is queued for execution at flush time, in source order alongside the file operations. Output streams live to the parent process's stdout/stderr.

- `in <path>` — pin the command's working directory to `<path>`. Without it, the command inherits the cwd of the `spudplate` process, which is rarely what you want once the template has created a project subdirectory. The path is a regular path expression — alias references and `{expr}` interpolation work, and the directory must exist by the time the command runs (errors otherwise). The cwd is restored after the command, even on failure.
- `when <condition>` — only run the command if the condition is true.

```
ask project "Project name?" string
mkdir {project} as proj
run "git init" in proj
ask repo_url "Origin URL?" string default ""
run "git remote add origin " + repo_url in proj when repo_url != ""
```

#### Trust prompt

Every `spudplate run <file.spud>` invocation that contains any `run` clauses prompts the user once, before any statement executes:

```
This template will execute the following shell commands via /bin/sh:
  1. "git init"
  2. "git remote add origin " + repo_url
Authorise these commands? [y/N]
```

The summary lists each command's source-form expression. Declining aborts cleanly with no side effects — no prompts run, no files are written, no commands execute. Conditional clauses appear in the summary regardless of the eventual `when` outcome (the validator can't predict runtime values), and commands inside `repeat` bodies are flagged as such.

The `--yes` (or `-y`) flag bypasses the prompt for non-interactive callers; the caller takes responsibility for having vetted the source. Once `spudplate install` lands, install-time consent will be recorded so installed templates run without re-prompting.

#### Failure handling

A non-zero exit, a signal-killed process, or a failure to invoke the shell raises a runtime error and aborts the rest of the flush. File operations queued *before* the failed command have already been performed; later operations do not run. This matches the existing partial-failure model documented under "Safety" below.

#### Security caveats

- **Shell injection via interpolation.** Commands are dispatched through `/bin/sh -c`. The trust prompt shows the literal source expression; the *evaluated* string is what executes. A command like `run "git clone " + url` looks safe in the prompt, but a malicious `url` answer (`; rm -rf $HOME`) would still be passed through. Treat any `ask`/`let` value flowing into a `run` command as untrusted and quote it appropriately in the command string, or restrict input via `options`.
- **Working directory.** Without `in <path>`, commands run in whatever directory `spudplate run` was invoked from — *not* a per-template sandbox. Templates that create a project subdirectory should pin every following command to it via `in <path>`.
- **No timeout.** A long-running command blocks the flush indefinitely. Templates running interactive commands (e.g. `vim`) will hand the terminal over.
- **No allowlist.** Any command is permitted once authorised. Privilege escalation (e.g. `sudo`) prompts for the user's password — that natural friction is the safety net for anything destructive.

---

## Expressions

Expressions appear in `let` values, `content` values, `default` values, `options`, `{...}` interpolations, and `when` conditions.

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

| Function              | Description                                              |
|-----------------------|----------------------------------------------------------|
| `lower(x)`            | Converts string to lowercase                             |
| `upper(x)`            | Converts string to uppercase                             |
| `trim(x)`             | Strips leading/trailing whitespace                       |
| `replace(s, from, to)`| Replaces every occurrence of `from` in `s` with `to`     |

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

Interpolation appears in two places with slightly different grammars:

- **Path expressions** support full `{expr}` interpolation (`mkdir week_{n}`, `mkdir {prefix}/notes`).
- **`content` values**, **`from` source files**, and files copied by `copy` support only bare `{ident}` substitution — no function calls, no arithmetic, no string concatenation inside the braces. For richer values in a `content` expression, use string concatenation (`content "v" + version`). Use `verbatim` (on `from` and `copy`) to copy file contents byte-for-byte without any substitution. A literal `{` or `}` in a non-verbatim source is a runtime error; switch the statement to `verbatim` if you need literal braces.

---

## Line Continuation

A `\` at the end of a line continues the statement on the next line. The parser sees a single logical line.

```
ask format "What output format?" string \
    options "pdf" "html" "latex" \
    default "pdf" \
    when use_docs
```

---

## Comments

Lines beginning with `#` are comments and are ignored by the parser.

```
# This is a comment
ask name "Your name?" string
```

---

## Safety

The interpreter refuses to write to any path that already existed before the run. It tracks paths created during execution and only allows overwriting those. This tracking is ephemeral — no metadata is left behind after the run finishes.

Combined with deferred file operations, this means a run either fully succeeds or makes no changes to your filesystem.

---

## Dry-run preview

`spudplate run --dry-run <file.spud>` runs every prompt and queues every action exactly like a real run, but instead of writing to disk it prints a tree of every path that would have been created:

```
Would create:
└── cool-thing/
    ├── README.md
    ├── src/
    │   └── main.cpp
    └── tests/
        └── test_main.cpp
```

Append-mode files appear once with a trailing `(append)` annotation. `mkdir from` and `copy` expand into the individual files they would create. `copy` destination-existence checks are skipped — dry-run cannot validate them without touching the filesystem. Conditional statements whose `when` clause evaluated to false are pruned from the queue, so the tree only shows what would actually happen.

Tree glyphs default to UTF-8 box-drawing characters (`├──`/`└──`/`│`). On terminals where the locale environment (`LC_ALL`, `LC_CTYPE`, or `LANG`) does not advertise UTF-8 support, spudplate falls back to plain ASCII (`|--`/`\\--`/`|`). The detection follows POSIX precedence — the first set variable wins.

---

## Full Example

```
ask project_name "Project name?" string
ask use_tests "Include tests?" bool default false
ask num_weeks "Estimated weeks?" int when use_tests
ask license "License?" string default "MIT"
ask use_claude "Set up Claude config?" bool default false

let slug = lower(trim(project_name))
let dir = slug + "-project"

mkdir {dir} as project
mkdir project/src
mkdir project/tests when use_tests

file project/README.md content "# " + project_name as readme
file {readme} append content "\nLicensed under " + license + "\n"
file project/src/main.cpp from templates/main.cpp
file project/tests/test_main.cpp from templates/test_main.cpp when use_tests

include claude_setup when use_claude
```
