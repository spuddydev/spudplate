# copy {#lang_stmt_copy}

```
copy <source> into <destination> [verbatim] [when <condition>]
```

Copies the contents of `<source>` into an **already-existing** `<destination>` directory. Errors if the destination does not exist.

[TOC]

## When to use copy

`copy` is the right tool when you want to **merge** content into a directory you have already created. It complements `mkdir from`, which creates a new directory and populates it.

| Goal                                                            | Statement                                |
|-----------------------------------------------------------------|------------------------------------------|
| Create a directory and populate it from one source              | `mkdir <path> from <source>`             |
| Add more content into a directory you already created           | `copy <source> into <existing_path>`     |

## Clauses

| Clause     | Effect                                                                              |
|------------|-------------------------------------------------------------------------------------|
| `verbatim` | Suppress `{ident}` substitution in copied file contents                             |
| `when`     | Skip the copy if the condition is false                                             |

## Examples

```
mkdir "templates" from "base_templates" as template_path

copy "philosophy_templates" into template_path when use_philosophy
copy "programming_templates" into template_path when use_programming
copy "math_templates" into template_path when use_math

copy "assets" into "{dir}/static" verbatim
```

The aliased path (`template_path`) is the conventional way to write this: declare the destination once, then merge content into it from multiple sources.

## Substitution

`{ident}` substitution is applied to copied file contents by default. Function calls, arithmetic, or string concatenation inside braces are **not** supported in source-file contents (only in `content` expressions). To skip substitution entirely, add `verbatim`:

```
copy "rendered_html" into "{dir}/site" verbatim
```

Binary files in the source tree are auto-detected and copied verbatim regardless.

## Destination must exist

`copy` errors if `<destination>` does not exist. Use `mkdir` (or `mkdir from`) first.

```
copy "extras" into "missing_dir"     # runtime error: missing_dir does not exist
```

This is intentional: `copy` is for layering content. If you want a fresh directory populated from one source, `mkdir from` is the right statement.

## Conditional copies

A `when`-gated `copy` lets you compose a directory from optional pieces:

```
mkdir "{dir}/templates" from "base_templates" as templates

copy "templates_python" into templates when language == "python"
copy "templates_rust" into templates when language == "rust"
copy "templates_go" into templates when language == "go"
```

Only one of the language-specific copies runs, but all three can sit unconditionally in the source.

## See also

- @ref lang_stmt_mkdir "mkdir": create the destination directory before merging into it.
- @ref lang_stmt_file "file": copy or generate single files instead of trees.
- @ref lang_paths "Path expressions": grammar for `<source>` and `<destination>`.
