# file {#lang_stmt_file}

```
file <path> [append] from <source> [verbatim] [mode <octal>] [when <condition>] [as <name>]
file <path> [append] content <expression>     [mode <octal>] [when <condition>] [as <name>]
```

Creates a file at `<path>`, or appends to one already created in the same run. The two forms differ in where the content comes from: a file already in the bundle (`from`) or an inline expression (`content`).

[TOC]

## Clauses

| Clause      | Effect                                                                              |
|-------------|-------------------------------------------------------------------------------------|
| `append`    | Append to the file rather than overwriting; the file must already have been created in this run |
| `from`      | Read the source file from the bundle. `{ident}` substitution applies unless `verbatim` |
| `content`   | Use the value of an inline expression. Full `{expr}` interpolation applies          |
| `verbatim`  | With `from`, suppress all substitution                                              |
| `mode`      | Set file permissions, masked to `0o0777`                                            |
| `when`      | Skip creation if the condition is false                                             |
| `as`        | Bind the resolved path; useful for conditional `append`                             |

## file content

Writes a file with content from an inline expression. The expression is evaluated to a string at run time. `{expr}` interpolation works inside string literals as described in @ref lang_types "Types and expressions".

```
file "{dir}/README.md" content "# " + project_name
file "{dir}/version.txt" content "v{major}.{minor}"
file "{dir}/empty" content ""
```

`content` requires a `string` value. Mixing a non-string into a `+` expression is rejected by the validator: convert through interpolation first.

## file from

Reads a file from the bundle and writes it to `<path>`. `{ident}` substitution is applied to file contents at run time (only bare identifiers, not full expressions).

```
file "{dir}/.gitignore" from "templates/gitignore"
file "{dir}/run.sh" from "templates/run.sh" mode 0755
```

To copy bytes verbatim (no substitution at all), add `verbatim`:

```
file "{dir}/main.tex" from "templates/main.tex" verbatim
```

The interpreter auto-detects binary content: a source file whose bytes are not valid UTF-8 is treated as `verbatim` regardless of the keyword, so binary assets such as PNGs or favicons just work.

## append

`append` adds to a file rather than overwriting. The file **must** have been created earlier in the same run; appending to a file that does not yet exist is a runtime error.

```
file "log.txt" content ""
file "log.txt" append content "Created by spudplate\n"
file "log.txt" append content "Project: {name}\n"
```

Common idiom: build up a sectioned README from optional pieces.

```
file "{dir}/README.md" content "# " + project_name as readme
file readme append content "\n\n## Notes\n" when use_notes
file readme append content "\n\n## Testing\n" when use_tests
```

The `as readme` alias makes the conditional appends terse and avoids repeating the path expression.

## as clauses

`as <name>` on a `file` binds the resolved file path. The most common reason to use it is conditional `append`. The conditional-alias rule applies: if the binding `file` carries a `when`, every reference must be guarded by an equivalent condition.

## mode and permissions

`mode` takes an octal literal. The value is masked to `0o0777`.

```
file "{dir}/run.sh" from "templates/run.sh" mode 0755
file "{dir}/secret" content "..." mode 0600
```

A file with no `mode` clause inherits the system default for newly created files.

## file from vs file content

| If you want to...                                              | Use                                       |
|----------------------------------------------------------------|-------------------------------------------|
| Insert a complete file from the bundle                          | `file ... from`                           |
| Generate a small file from a string expression                  | `file ... content`                        |
| Embed runtime values into the body of a templated file          | `file ... from` (uses `{ident}`)          |
| Use full expression evaluation inside the file body             | `file ... content` (uses `{expr}`)        |

For mostly-static text with a few placeholders, `from` is the cleaner choice. For dynamically composed content, `content` is.

## See also

- @ref lang_stmt_mkdir "mkdir": create the directory the file lives in.
- @ref lang_stmt_copy "copy": copy a tree of files at once.
- @ref lang_paths "Path expressions": grammar for `<path>` and `<source>`.
- @ref lang_types "Types and expressions": interpolation rules in `content` vs `from`.
