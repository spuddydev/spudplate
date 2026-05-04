#ifndef SPUDPLATE_SPUDPACK_H
#define SPUDPLATE_SPUDPACK_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace spudplate {

/**
 * @brief One bundled asset entry inside a spudpack.
 *
 * `path` is normalised - forward-slash separators, no leading `/`, no `.` or
 * `..` segments, no embedded NUL. A trailing `/` means "empty leaf
 * directory" and the corresponding `data` is empty.
 */
struct SpudpackAsset {
    std::string path;                ///< Normalised asset path inside the pack.
    std::uint16_t mode;              ///< POSIX mode bits, masked to `0o0777`.
    std::vector<std::uint8_t> data;  ///< Raw asset bytes; empty for an empty leaf directory.
};

/**
 * @brief One bundled dependency inside a spudpack.
 *
 * Each dep is the complete byte stream of another spudpack referenced by an
 * `include <name>` statement in the parent program. Names match the bare
 * identifier used in `include` and follow the same rules as installed
 * template names: nonempty, no `/`, no NUL, not `.` or `..`. Bytes are kept
 * opaque - the consumer decodes them through `spudpack_decode` when an
 * `include` statement actually fires at runtime.
 */
struct SpudpackDep {
    std::string name;                ///< Bare include name, matching `<name>.spp` on the install root.
    std::vector<std::uint8_t> bytes; ///< Full byte stream of the bundled dep's spudpack.
};

/**
 * @brief A decoded spudpack - the source text, the opaque compiled program,
 * every bundled asset, and every bundled dependency.
 *
 * `program_bytes` is held opaquely; this header does not include the
 * binary-serializer header so the codec stays decoupled from the AST.
 * Decoding `program_bytes` into a `Program` is the caller's job.
 */
struct Spudpack {
    std::string source;                       ///< Original `.spud` source text.
    std::vector<std::uint8_t> program_bytes;  ///< Opaque serialised AST; decoded by the binary serializer.
    std::vector<SpudpackAsset> assets;        ///< Every bundled asset referenced by the program.
    std::vector<SpudpackDep> deps;            ///< Every bundled dependency referenced by `include` statements.
    std::uint8_t version{3};                  ///< Spudpack format version that produced these bytes. Threaded into the binary serializer so trailing-optional fields decode correctly across versions.
};

/**
 * @brief Raised on any encode or decode failure.
 *
 * `offset()` carries the byte offset at which decoding gave up when the
 * failure was decoder-side; encoder-side failures leave it empty.
 */
class SpudpackError : public std::runtime_error {
  public:
    /** @brief Construct with a message and an optional decode offset. */
    SpudpackError(std::string message, std::optional<std::size_t> offset = std::nullopt);
    /** @brief Byte offset where decoding failed, if known. */
    std::optional<std::size_t> offset() const noexcept { return offset_; }

  private:
    std::optional<std::size_t> offset_;
};

/**
 * @brief Encode a `Spudpack` into a tightly packed byte stream.
 *
 * Layout: magic `"SPUD"` (4 bytes), version `u8` (currently `3`; `1` and
 * `2` are still accepted on decode for backward compatibility), flags
 * `u8 = 0`, `varint`+`bytes` source, `varint`+`bytes` program, `varint`
 * asset_count, per asset (`varint`+`bytes` path, `u16 LE` mode,
 * `varint`+`bytes` data), `varint` dep_count, per dep (`varint`+`bytes`
 * name, `varint`+`bytes` blob), `u32 LE` CRC32 over `[0, size-4)`. Packs
 * decoded as v1 or v2 must report `dep_count = 0`; v3 may carry deps.
 */
std::vector<std::uint8_t> spudpack_encode(const Spudpack& pack);

/**
 * @brief Decode a byte stream into a `Spudpack`.
 *
 * Validates magic, version, flags, every varint length against the input
 * bounds, the per-asset and per-dep cap (256 MiB), the total file cap
 * (2 GiB), the asset-count cap (`1 << 20`), the dep-count cap (`1 << 10`),
 * each asset path against the normalisation rules, each dep name against
 * the bare-identifier rules, mode bits against `0o7777`, and the trailing
 * CRC32. v1 and v2 packs are rejected if `dep_count != 0`. Every failure
 * throws `SpudpackError` with the byte offset at which decoding gave up.
 */
Spudpack spudpack_decode(const std::uint8_t* data, std::size_t size);

/**
 * @brief Write a `Spudpack` to disk.
 *
 * **Not atomic** - callers that need atomic install must write to a
 * temporary path and rename themselves.
 */
void spudpack_write_file(const std::filesystem::path& path, const Spudpack& pack);

/** @brief Read and decode a spudpack file. */
Spudpack spudpack_read_file(const std::filesystem::path& path);

/**
 * @brief Normalise an asset path string.
 *
 * Strips a leading `./`, collapses `//` runs, rejects `..` segments and
 * embedded NUL, and rejects absolute paths. The result is forward-slash
 * separated, has no leading `/`, no `.` or `..` segments, and no embedded
 * NUL. A trailing `/` is preserved if present in the input. Throws
 * `SpudpackError` (without an offset) on inputs that cannot be normalised.
 */
std::string normalize_asset_path(std::string_view raw);

/**
 * @brief Predicate form of `normalize_asset_path`.
 *
 * Returns true iff the input is already in the form `normalize_asset_path`
 * would produce. Used by the codec decoder to validate asset entries.
 */
bool is_normalized_asset_path(std::string_view path) noexcept;

}  // namespace spudplate

#endif  // SPUDPLATE_SPUDPACK_H
