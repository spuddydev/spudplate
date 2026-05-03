# mkdir {#lang_stmt_mkdir}

```
mkdir <path> [from <source>] [verbatim] [mode <octal>] [when <condition>] [as <name>]
```

Creates a directory at `<path>`, with `mkdir -p` semantics: intermediate directories are created automatically if missing.

[TOC]

## Clauses

| Clause      | Effect                                                                              |
|-------------|-------------------------------------------------------------------------------------|
| `from`      | Populate the new directory from `<source>`, recursively                             |
| `verbatim`  | With `from`, suppress `{ident}` substitution in copied file contents                |
| `mode`      | Set directory permissions, masked to `0o0777`                                       |
| `when`      | Skip creation if the condition is false                                             |
| `as`        | Bind the resolved path to a name for later reuse                                    |

## Empty mkdir

The simplest form just creates a directory.

```
mkdir "static"
mkdir "src/main"
mkdir "{slug}/notes"
mkdir "deeply/nested/path"        # all parents created automatically
```

The path follows the @ref lang_paths "path expression" grammar. Quote any literal segment; bare identifiers are treated as variable references.

## mkdir from

Populates the new directory from a source tree in the bundle (or, when running directly from a `.spud` file, on the filesystem relative to the current working directory).

```
mkdir "templates" from "base_templates"
mkdir "{dir}/docs" from "templates/docs" mode 0755
```

Source files are read at install time and embedded in the `.spp`. At run time, `{ident}` substitution is applied to each copied file's contents (binary files are auto-detected and copied verbatim regardless).

Add `verbatim` to suppress all substitution:

```
mkdir "assets" from "asset_tree" verbatim
```

## mkdir from vs copy into

`mkdir from` creates a new directory and populates it. The destination must not already exist.

`copy into` requires its destination to **already** exist; it is for merging additional content into a directory you have already created. The two are not interchangeable; using one where the other is needed produces a runtime error.

A common pattern combines them:

```
mkdir "templates" from "base_templates" as template_path
copy "philosophy_templates" into template_path when use_philosophy
copy "programming_templates" into template_path when use_programming
```

## as clauses

The `as <name>` clause binds the resolved path to a name. Subsequent statements can use that name as a bare path segment.

```
mkdir "static" as static_path
mkdir static_path/"notes"
mkdir static_path/"images"
```

When `as` is on a conditional `mkdir`, the alias is conditional too; references must be guarded by an equivalent condition. See @ref lang_scoping "Scoping" for the equivalence rules.

## mode and permissions

`mode` takes an octal literal. The value is masked to `0o0777`, so setuid and setgid bits cannot be set.

```
mkdir "private" mode 0700
mkdir "shared" mode 0755
```

## See also

- @ref lang_stmt_copy "copy": merge content into an existing directory.
- @ref lang_stmt_file "file": create files inside the directory.
- @ref lang_paths "Path expressions": full grammar for `<path>` and `<source>`.
- @ref lang_pitfalls "Pitfalls": reserved-word path segments and quoting rules.
