# Changelog

This file records notable changes per release. Older versions live on the
[GitHub Releases](https://github.com/spuddydev/spudplate/releases) page.

## v0.4.0

### include statements run in place against bundled dependencies

`include` is no longer a stub. The statement now:

- Bundles the named dependency's bytes into the parent at install time, so a
  template you share carries every template it depends on. The recipient
  does not need the dependency installed separately.
- Runs the dependency inline at the include point, with isolated variable
  scope. Prompts interleave with the parent's in source order, and
  filesystem operations join the parent's deferred queue, so the whole tree
  either commits together or leaves the disk untouched.

```
ask use_claude "Set up Claude config?" bool default false
include claude_setup when use_claude
ask name "Project name?" string
```

The `when` clause is evaluated at runtime and skips the entire dependency
when false. Nested includes are capped at sixteen levels deep.

### Spudpack format v3

The spudpack on-disk format gains a real dependency section. Each dep is a
bare-identifier name plus the full bytes of another spudpack. v1 and v2
packs still decode unchanged and continue to require `dep_count == 0`. See
the [Spudpack Binary Format](docs/spudpack-format.md) reference for the
full layout.

### Author guidance

If you ship a template that uses `include`, install the dependency first,
then install your parent. The parent is then self-contained and can be
shared as a single `.spp`.

### Prompt counter follows includes

`(n/m)` is now one denominator across the whole include tree. A prompt
in a bundled dep continues the parent's count toward the same total, and
sits at one nesting level deeper of indent. Nested includes stack the
indent further so the user can see the depth at a glance.

### Monotonic version tags

Every installed template carries a `version_tag` that increments on each
install whose content differs from what is already on disk:

- First install of a name is v1.
- Reinstalling identical content is a no-op and prints `already up to
  date`.
- Reinstalling changed content bumps to v2, v3, and so on.
- The previous on-disk pack is copied to `<install-root>/.archive/<name>.v<N>.spp`
  before being overwritten.

`spudplate list` prints `name (vN)` per line. `spudplate inspect <name>`
prints the template's version, the version of every bundled dep, and
the original source.

### Sticky dependencies and the `--update-deps` flag

When reinstalling a parent, the bundler defaults to reusing the dep
bytes the previous parent already carried. Editing the parent's source
without otherwise touching its deps no longer silently swaps a dep for
the latest installed version. To opt in to a refresh:

```
spudplate install bar.spud --update-deps foo,baz
```

lists the unpinned deps to re-read from the install root.

### Hard pins in source

`include foo@N` resolves to that exact version. The bundler tries the
current install root first; if its `version_tag` is not `N`, it falls
back to `<install-root>/.archive/foo.vN.spp`. If neither carries the
pinned version, the install fails. Pinned deps are unaffected by
`--update-deps`; the flag is silently ignored for them with a one-line
note.

### Drift warning at run time

`spudplate run <name>` compares each top-level bundled dep's
`version_tag` against the currently-installed copy at
`<install-root>/<name>.spp`. A mismatch produces one warning line per
drifted dep. The bundled bytes always run regardless; the warning is
informational and points out that reinstalling the parent will pick up
the newer (or older) dep.

### `uninstall` clears archive entries

`spudplate uninstall foo` now also removes
`<install-root>/.archive/foo.v*.spp`. Archives for other names are
untouched.
