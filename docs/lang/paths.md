# Path Expressions {#lang_paths}

`mkdir`, `file`, `copy`, and the `from` and `in` clauses on those statements all take a **path expression**: a slash-separated sequence of segments that describes a file or directory, relative to the current working directory at run time. This page describes the path grammar, the two kinds of segment, the rules for aliases, and how `let`-bound strings work as path roots.

[TOC]

## Segment kinds

A path is one or more segments separated by `/`. Each segment is either:

1. **A bare identifier**, which is always a variable reference. The name must resolve to a path alias (declared with `as` on a `mkdir` or `file`) or to a `let`-bound string that is in scope. Unresolved bare identifiers are rejected at validate time.
2. **A quoted string literal**, which is a literal segment. A quoted segment may contain `/` characters (one quoted token then contributes one or more components, split on the slashes), and may contain `{expr}` interpolations.

```
mkdir "static"
mkdir "static/notes"
mkdir "static/notes/week_{n}"
file "src/main.cpp" from "templates/main.cpp"
```

## Where interpolation works

`{expr}` interpolation works **only inside a quoted segment**. A bare `{var}` outside quotes is a parse error.

```
mkdir "week_{n}"          # ok
mkdir "{dir}/src"         # ok, quoted segment with interpolation and a slash
mkdir {n}                 # parse error: '{...}' is only allowed inside quoted path strings
```

When you need to combine an alias with an interpolation, quote the parts that need braces:

```
mkdir "static" as static_path
mkdir static_path/"week_{n}"
```

## `mkdir -p` semantics

`mkdir` always creates intermediate directories. `mkdir "a/b/c"` creates `a`, `a/b`, and `a/b/c` in turn if any do not exist. There is no separate `mkdir -p` form.

A `mkdir` whose path resolves to an already-existing path that the current run did not create is a runtime error. The interpreter only allows writing to paths it created itself within the same run, so there is no risk of silently overwriting a directory that pre-existed.

## Path aliases with `as`

The `as <name>` clause on `mkdir` or `file` binds the resolved path to a name. Subsequent path expressions may use that name as a bare identifier segment.

```
mkdir "static" as static_path
mkdir "notes" as notes_path
mkdir static_path/notes_path/"week_{n}"   # both aliases used as bare segments
```

On `file`, an alias binds the file path. The most common use is conditional `append`:

```
file "{dir}/README.md" content "# " + project_name as readme
file readme append content "## Notes\n" when use_notes
file readme append content "## Testing\n" when use_tests
```

An `as` clause is optional. If you do not need to refer back to the path, omit it.

### Alias scoping under `when`

When a `mkdir` or `file` carries a `when` clause, the alias it binds is **conditional**. It cannot be used outside a statement guarded by an equivalent condition. The validator rejects references that escape the gate.

```
mkdir "tests" as tests_path when use_tests
file tests_path/"README.md" content "# Tests" when use_tests   # ok
file tests_path/"main.cpp" from "templates/test.cpp"           # error: missing matching when
```

Conditions are compared after normalisation. Equivalences the validator recognises:

- For a `bool` variable `b`, the forms `b`, `b == true`, and `not not b` are all the same condition. Likewise `not b`, `b == false`, and `b != true` match each other.
- Other comparisons (`format == "latex"`, `n > 4`) are compared structurally.
- Commutativity is **not** considered equivalent: `a and b` does not match `b and a`. This is a known limitation and may change in future.

If the binding statement has no `when` clause, the alias is unconditional and can be used anywhere.

## `let`-bound strings as path roots

A string declared with `let` can be used as a bare path segment, just like an alias.

```
let project_dir = lower(slug) + "-project"
mkdir project_dir/"src"
file project_dir/"README.md" content "# " + project_name
```

A `let` value that contains `/` is appended **verbatim** as a single logical segment, contributing one or more directory levels at runtime:

```
let team_dir = "team/projects"
mkdir team_dir/"alpha"      # creates team/projects/alpha
```

This differs from a quoted literal, which is split on `/` at parse time. The bundler treats a `let`-bound root as fully dynamic, so only the **static prefix** made of quoted literals is walked at install time. If a `from` clause's source path begins with a `let` segment, the bundler cannot pre-walk it and the source must already be reachable on disk at install time.

## Source paths

The `from` clause on `mkdir` and `file` takes a path expression that names a file or directory inside the bundle (or, when running directly from a `.spud` file with no install, on the filesystem relative to the current directory).

```
file "main.cpp" from "templates/main.cpp"
mkdir "templates" from "base_templates"
copy "philosophy_templates" into "templates"
```

The same path grammar applies to source paths as to destination paths: bare segments must be aliases or `let`-bound roots, quoted segments may contain `/` and interpolations.

## Reserved words cannot appear as bare path segments

The lexer recognises every reserved word (see @ref lang_lexical "Lexical Structure") before path-expression parsing begins. As a result, a directory named the same as a keyword (`include`, `file`, `from`, `as`, `end`, etc.) cannot appear unquoted in a path. Quote the segment to use it as a literal:

```
mkdir "src/include"          # ok, quoted
mkdir src/include            # error: 'include' is parsed as the include keyword
```

The full list of reserved words is in @ref lang_lexical "Lexical Structure". @ref lang_pitfalls "Pitfalls" walks through the practical impact and the workarounds.

## Empty leaf directories

A path that ends at a directory with no descendants is preserved as an empty leaf in the bundle. At install time it is recorded with a trailing `/` and zero data length, and at run time `mkdir` recreates it. You do not need to use a separate keyword to keep an empty directory: `mkdir "static/empty_dir"` does the right thing.

## Path normalisation

Every path is normalised before it is stored or written:

- Forward slashes are the only separator. A backslash is a literal byte, not a path separator.
- A leading `/` is stripped: paths are always relative to the current working directory.
- `..` segments are rejected at validate time. Templates cannot escape their working directory.
- Empty segments (caused by `//`) are collapsed.

Modes set with `mode <octal>` are masked to `0o0777` so a template cannot ship setuid or setgid bits.
