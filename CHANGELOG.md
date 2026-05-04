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
