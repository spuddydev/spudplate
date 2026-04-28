# CLI Reference

```
spudplate <command> [args...]
```

Every subcommand spudplate ships, with its full flag set.

---

## install

```
spudplate install [--yes] <file.spud>
```

Validates and stores a template. Bundles every asset the template references into a single `<name>.spp` file under the install root.

| Flag | Effect |
|------|--------|
| `--yes`, `-y` | Skip the overwrite confirmation when a template of the same name already exists. |

`install` rejects pre-built `.spp` input - only `.spud` sources can be bundled.

---

## run

```
spudplate run [--dry-run] [--yes] [--no-timeout] <name|file.spud|file.spp>
```

Runs an installed template by name, or runs a `.spud` or `.spp` file directly.

| Flag | Effect |
|------|--------|
| `--dry-run` | Walk the program and print the questions and actions without writing any files. |
| `--yes`, `-y` | Auto-accept question prompts that have a default. |
| `--no-timeout` | Disable per-`run` timeouts for this invocation (default is 60 seconds per shell command). |

`run` decides whether the argument is a path or an installed name. An argument containing `/` or ending in `.spud` or `.spp` is treated as a path; everything else is looked up as `<install-root>/<arg>.spp`.

---

## validate

```
spudplate validate <file.spud>
```

Parses and validates a `.spud` file without installing anything. Useful in CI and editor integrations. Same semantic checks as `install`. Also available as `check`.

---

## list

```
spudplate list
```

Prints every installed template, one per line.

---

## inspect

```
spudplate inspect <name>
```

Prints the original `.spud` source captured at install time. Accepts a bare name only - not a path.

---

## uninstall

```
spudplate uninstall <name>
```

Removes an installed template. Accepts a bare name only.

---

## version

```
spudplate version
```

Prints the spudplate version. `--version` works as an alias.

---

## update

```
spudplate update [--yes]
```

Fetches and installs the latest spudplate release by re-running the install script.

| Flag | Effect |
|------|--------|
| `--yes`, `-y` | Skip the confirmation prompt. |

---

## Environment variables

| Variable | Effect |
|----------|--------|
| `SPUDPLATE_HOME` | Explicit override for the install root. Highest precedence; used by tests and dev setups. |
| `XDG_DATA_HOME` | Falls back to `$XDG_DATA_HOME/spudplate` if set. |
| `HOME` | Final fallback: `$HOME/.local/share/spudplate`. |

If none of the above is set, install-root operations fail with a clear diagnostic.

---

## Exit codes

| Code | Meaning |
|------|---------|
| 0 | Success. |
| 1 | Generic usage or runtime error. |
| 2 | Parse error in a `.spud` file. |
| 3 | Semantic (validator) error in a `.spud` file. |
| 5 | I/O error - file not found, permission denied, install root unreachable. |

---

## Legacy installs

A directory-shaped install (`<install-root>/<template-name>/template.spud`) from a pre-`.spp` build is recognised. `run` and `inspect` print `legacy install '...'; reinstall to upgrade` and exit non-zero. `uninstall` falls back to removing the legacy directory. `install` over a legacy install removes the directory after a successful rename.
