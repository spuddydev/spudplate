# Spudpack Binary Format

A `.spp` file is a single self-contained binary holding the original `.spud` source, the compiled program (binary AST), and every asset the program references. One file is the install, run, and share artefact.

This document is the on-disk contract. The encoder lives in `src/spudpack.cpp`; the decoder in the same file performs every check described below.

---

## At a glance

```
magic    "SPUD"        4 bytes
version  u8            1 byte    currently 2; 1 still accepted on decode
flags    u8            1 byte    must be 0
source   length+bytes            varint length, raw UTF-8 source
program  length+bytes            varint length, opaque AST bytes
asset_count varint                number of asset records
per asset:
  path   length+bytes            varint length, normalised path
  mode   u16 LE                  POSIX mode bits, masked to 0o0777
  data   length+bytes            varint length, raw bytes
dep_count varint                  reserved, must be 0
trailer  u32 LE                  CRC32 over [0, size-4)
```

All multi-byte integers are little-endian. Variable-length integers (`varint`) are unsigned LEB128 capped at 10 bytes.

---

## Versions

| Version | Change |
|---------|--------|
| 1 | Original format. |
| 2 | `RunStmt` gains a trailing optional `timeout` field (one present-flag byte plus, if set, the encoded expression). Packs without `run` statements are byte-identical to v1 once the version byte is bumped. |

The encoder always writes the latest version. The decoder accepts v1 and v2; the version is threaded through to the binary deserialiser so trailing-optional fields decode correctly across versions. Adding a future v3 is mechanical: append the new fields, bump `kVersion`, branch the decoder.

---

## Caps

The decoder rejects malformed input before any allocation. Every cap below is enforced against the declared length, not the actual bytes consumed.

| Cap | Value |
|-----|-------|
| Per-asset bytes | 256 MiB |
| Total file size | 2 GiB |
| Asset count | 2^20 (1,048,576) |
| Varint length | 10 bytes |
| Recursion depth (binary AST) | 256 |

A length that exceeds remaining input throws before the read; an asset-count varint that exceeds remaining input throws before any vector reservation.

---

## Asset paths

Every asset path is normalised before encode and re-validated on decode. The rules:

- Forward-slash separators only.
- No leading `/`.
- No `.` or `..` segments.
- No embedded NUL.
- A trailing `/` denotes an empty leaf directory, in which case `data` must be empty.

`normalize_asset_path` strips a leading `./`, collapses `//` runs, and rejects any input that violates the rules above. `is_normalized_asset_path` is the predicate form used by the decoder.

---

## Asset modes

The encoder masks each asset's POSIX mode bits to `0o0777` so templates cannot ship setuid, setgid, or sticky payloads. The decoder additionally rejects any mode with bits set above `0o7777`, leaving forward room without accepting nonsense.

---

## Integrity

The CRC32 trailer covers everything from byte 0 up to but not including the trailer itself. The decoder computes the CRC over the same range and throws if the stored value does not match. Any single bit-flip anywhere in the file is detected.

The CRC is checked alongside structural validation, not after, so a hostile pack with a correct CRC and a corrupt body still throws on the structural check.

---

## Errors

Every encode and decode failure throws `SpudpackError` from `<spudplate/spudpack.h>`. Decoder-side errors carry the byte offset at which decoding gave up; encoder-side errors leave the offset empty.

Common failure cases:

- `not a spudpack` - magic mismatch.
- `unsupported spudpack version N` - version outside `[kMinVersion, kVersion]`.
- `unsupported spudpack flags N` - flags byte non-zero.
- `spudpack truncated` - length read past the end.
- `spudpack varint exceeds 10 bytes` - oversize varint.
- `asset_count exceeds maximum` - hostile asset count.
- `spudpack does not support deps` - non-zero `dep_count`.
- `spudpack CRC mismatch` - trailer does not match recomputed CRC.

---

## What the format does not cover

- **Compression.** Assets are stored uncompressed. A future version may add a flag bit and a compressed-asset variant.
- **Signatures.** There is no signing or authentication. The CRC is integrity, not authenticity.
- **Dependencies.** `dep_count` is reserved at 0 today; the planned `include` bundling work will use it.
