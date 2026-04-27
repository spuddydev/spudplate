#include "spudplate/spudpack.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>

#include "spudplate/crc32.h"

namespace spudplate {

namespace {

// Wire constants. v1 of the on-disk format uses magic "SPUD" followed by a
// version byte (must be 1), a flags byte (must be 0), and the rest of the
// fields described in the header doc comment.
constexpr std::array<std::uint8_t, 4> kMagic = {'S', 'P', 'U', 'D'};
constexpr std::uint8_t kVersion = 1;
constexpr std::uint8_t kFlags = 0;

// Per-asset and per-file caps. Both apply to declared lengths inside the
// stream and are checked before any allocation so a malformed header
// cannot trigger a multi-gigabyte `vector::reserve`.
constexpr std::size_t kMaxAssetBytes = std::size_t{256} * 1024 * 1024;
constexpr std::size_t kMaxTotalBytes = std::size_t{2} * 1024 * 1024 * 1024;
constexpr std::size_t kMaxAssetCount = std::size_t{1} << 20;

// Producer convention: spudplate's own bundler masks asset modes to 0o0777
// before encode. The decoder additionally rejects anything outside 0o7777
// to give us forward room without accepting nonsense.
constexpr std::uint16_t kModeMask = 07777;

// LEB128 unsigned varint writer. The reader (added with the decoder) caps
// length at 10 bytes; the writer's longest encoding for a 64-bit value is
// also 10 bytes, so the cap stays consistent on both sides.
void write_varint(std::vector<std::uint8_t>& out, std::uint64_t value) {
    while (value >= 0x80) {
        out.push_back(static_cast<std::uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<std::uint8_t>(value));
}

void write_u16_le(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void write_u32_le(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

void write_bytes(std::vector<std::uint8_t>& out, const void* data, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    out.insert(out.end(), p, p + size);
}

void write_length_prefixed(std::vector<std::uint8_t>& out, const void* data,
                           std::size_t size) {
    write_varint(out, static_cast<std::uint64_t>(size));
    write_bytes(out, data, size);
}

class Reader {
  public:
    Reader(const std::uint8_t* data, std::size_t size) noexcept
        : data_(data), size_(size) {}

    std::size_t offset() const noexcept { return offset_; }
    std::size_t remaining() const noexcept { return size_ - offset_; }

    std::uint8_t read_u8() {
        if (remaining() < 1) {
            throw SpudpackError("spudpack truncated", offset_);
        }
        return data_[offset_++];
    }

    std::uint16_t read_u16_le() {
        if (remaining() < 2) {
            throw SpudpackError("spudpack truncated", offset_);
        }
        std::uint16_t v = static_cast<std::uint16_t>(data_[offset_]) |
                          static_cast<std::uint16_t>(data_[offset_ + 1]) << 8;
        offset_ += 2;
        return v;
    }

    std::uint32_t read_u32_le() {
        if (remaining() < 4) {
            throw SpudpackError("spudpack truncated", offset_);
        }
        std::uint32_t v = static_cast<std::uint32_t>(data_[offset_]) |
                          static_cast<std::uint32_t>(data_[offset_ + 1]) << 8 |
                          static_cast<std::uint32_t>(data_[offset_ + 2]) << 16 |
                          static_cast<std::uint32_t>(data_[offset_ + 3]) << 24;
        offset_ += 4;
        return v;
    }

    // LEB128 unsigned varint, capped at 10 bytes. Throws SpudpackError so
    // overlong varints encountered while decoding the spudpack envelope
    // do not surface as binary-serializer errors.
    std::uint64_t read_varint() {
        std::size_t start = offset_;
        std::uint64_t result = 0;
        int shift = 0;
        for (int i = 0; i < 10; ++i) {
            if (remaining() < 1) {
                throw SpudpackError("spudpack truncated", offset_);
            }
            std::uint8_t b = data_[offset_++];
            result |= static_cast<std::uint64_t>(b & 0x7F) << shift;
            if ((b & 0x80) == 0) return result;
            shift += 7;
        }
        throw SpudpackError("spudpack varint exceeds 10 bytes", start);
    }

    // Read a varint length and validate it against the remaining input
    // before any allocation. The sum-overflow guard catches a hand-crafted
    // header claiming `length = SIZE_MAX`, which would otherwise wrap to a
    // small value during arithmetic.
    std::size_t read_checked_length() {
        std::size_t at = offset_;
        std::uint64_t len = read_varint();
        if (len > std::numeric_limits<std::size_t>::max()) {
            throw SpudpackError("spudpack length overflow", at);
        }
        std::size_t lz = static_cast<std::size_t>(len);
        if (offset_ > size_ || lz > size_ - offset_) {
            throw SpudpackError("spudpack truncated", at);
        }
        return lz;
    }

    void read_bytes_into(std::string& out, std::size_t n) {
        out.assign(reinterpret_cast<const char*>(data_ + offset_), n);
        offset_ += n;
    }

    void read_bytes_into(std::vector<std::uint8_t>& out, std::size_t n) {
        out.assign(data_ + offset_, data_ + offset_ + n);
        offset_ += n;
    }

  private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t offset_ = 0;
};

}  // namespace

SpudpackError::SpudpackError(std::string message, std::optional<std::size_t> offset)
    : std::runtime_error(std::move(message)), offset_(offset) {}

bool is_normalized_asset_path(std::string_view path) noexcept {
    if (path.empty()) return false;
    if (path.front() == '/') return false;
    if (path.find('\0') != std::string_view::npos) return false;

    // Trailing slash is permitted on empty-leaf directory entries. Strip it
    // for the segment scan so the predicate matches encoded entries with or
    // without it.
    std::string_view body = path;
    if (body.size() > 1 && body.back() == '/') body.remove_suffix(1);

    std::size_t i = 0;
    while (i < body.size()) {
        std::size_t j = body.find('/', i);
        if (j == std::string_view::npos) j = body.size();
        std::string_view seg = body.substr(i, j - i);
        if (seg.empty()) return false;       // double slash
        if (seg == ".") return false;        // current-dir segment
        if (seg == "..") return false;       // parent-dir segment
        i = j + 1;
    }
    return true;
}

std::string normalize_asset_path(std::string_view raw) {
    if (raw.empty()) {
        throw SpudpackError("asset path is empty");
    }
    if (raw.find('\0') != std::string_view::npos) {
        throw SpudpackError("asset path contains embedded NUL");
    }
    if (raw.front() == '/') {
        throw SpudpackError("asset path is absolute");
    }

    bool trailing_slash = raw.size() > 1 && raw.back() == '/';
    std::string_view body = raw;
    if (trailing_slash) body.remove_suffix(1);

    // Strip a single leading "./" so "./a/b" normalises to "a/b". Mid-path
    // "." segments are dropped during the segment walk below.
    if (body.size() >= 2 && body[0] == '.' && body[1] == '/') {
        body.remove_prefix(2);
    }

    std::string out;
    out.reserve(body.size());
    std::size_t i = 0;
    while (i < body.size()) {
        std::size_t j = body.find('/', i);
        if (j == std::string_view::npos) j = body.size();
        std::string_view seg = body.substr(i, j - i);
        i = j + 1;
        if (seg.empty()) continue;          // collapse "//" runs
        if (seg == ".") continue;           // drop "." segments
        if (seg == "..") {
            throw SpudpackError("asset path contains '..' segment");
        }
        if (!out.empty()) out.push_back('/');
        out.append(seg);
    }

    if (out.empty()) {
        throw SpudpackError("asset path normalises to empty");
    }
    if (trailing_slash) out.push_back('/');
    return out;
}

std::vector<std::uint8_t> spudpack_encode(const Spudpack& pack) {
    if (pack.assets.size() > kMaxAssetCount) {
        throw SpudpackError("spudpack asset_count exceeds maximum");
    }

    std::vector<std::uint8_t> out;
    out.reserve(pack.source.size() + pack.program_bytes.size() + 64);

    write_bytes(out, kMagic.data(), kMagic.size());
    out.push_back(kVersion);
    out.push_back(kFlags);

    write_length_prefixed(out, pack.source.data(), pack.source.size());
    write_length_prefixed(out, pack.program_bytes.data(), pack.program_bytes.size());

    write_varint(out, static_cast<std::uint64_t>(pack.assets.size()));
    for (const SpudpackAsset& asset : pack.assets) {
        if (!is_normalized_asset_path(asset.path)) {
            throw SpudpackError("spudpack asset path is not normalised: " + asset.path);
        }
        if (asset.data.size() > kMaxAssetBytes) {
            throw SpudpackError("spudpack asset exceeds 256MiB cap: " + asset.path);
        }
        if (!asset.path.empty() && asset.path.back() == '/' && !asset.data.empty()) {
            throw SpudpackError(
                "spudpack empty-leaf path carries data: " + asset.path);
        }
        if ((asset.mode & ~kModeMask) != 0) {
            throw SpudpackError(
                "spudpack asset mode has reserved bits set: " + asset.path);
        }
        write_length_prefixed(out, asset.path.data(), asset.path.size());
        write_u16_le(out, asset.mode);
        write_length_prefixed(out, asset.data.data(), asset.data.size());
    }

    // dep_count: spudpack v1 does not bundle dependencies. Reserved as a
    // varint (rather than a fixed byte) so a future version that wants
    // hundreds of deps does not need a layout change.
    write_varint(out, 0);

    if (out.size() + 4 > kMaxTotalBytes) {
        throw SpudpackError("spudpack exceeds 2GiB total cap");
    }

    std::uint32_t crc = crc32(out.data(), out.size());
    write_u32_le(out, crc);
    return out;
}

Spudpack spudpack_decode(const std::uint8_t* data, std::size_t size) {
    if (size > kMaxTotalBytes) {
        throw SpudpackError("spudpack exceeds 2GiB total cap", 0);
    }
    if (size < kMagic.size() + 2 + 4) {
        // Need at least magic + version + flags + 4-byte CRC trailer.
        throw SpudpackError("spudpack truncated", 0);
    }

    Reader r(data, size - 4);  // CRC trailer is verified separately

    if (std::memcmp(data, kMagic.data(), kMagic.size()) != 0) {
        throw SpudpackError("not a spudpack", 0);
    }
    r.read_u8(); r.read_u8(); r.read_u8(); r.read_u8();  // skip magic

    std::uint8_t version = r.read_u8();
    if (version != kVersion) {
        throw SpudpackError(
            "unsupported spudpack version " + std::to_string(version), 4);
    }
    std::uint8_t flags = r.read_u8();
    if (flags != kFlags) {
        throw SpudpackError(
            "unsupported spudpack flags " + std::to_string(flags), 5);
    }

    Spudpack pack;

    std::size_t source_len = r.read_checked_length();
    r.read_bytes_into(pack.source, source_len);

    std::size_t program_len = r.read_checked_length();
    r.read_bytes_into(pack.program_bytes, program_len);

    std::size_t asset_count_at = r.offset();
    std::uint64_t asset_count_raw = r.read_varint();
    if (asset_count_raw > kMaxAssetCount) {
        throw SpudpackError(
            "spudpack asset_count exceeds maximum", asset_count_at);
    }
    std::size_t asset_count = static_cast<std::size_t>(asset_count_raw);
    pack.assets.reserve(asset_count);

    for (std::size_t i = 0; i < asset_count; ++i) {
        SpudpackAsset asset;

        std::size_t path_at = r.offset();
        std::size_t path_len = r.read_checked_length();
        r.read_bytes_into(asset.path, path_len);
        if (!is_normalized_asset_path(asset.path)) {
            throw SpudpackError(
                "spudpack asset path is not normalised", path_at);
        }

        std::size_t mode_at = r.offset();
        asset.mode = r.read_u16_le();
        if ((asset.mode & ~kModeMask) != 0) {
            throw SpudpackError(
                "spudpack asset mode has reserved bits set", mode_at);
        }

        std::size_t data_len_at = r.offset();
        std::size_t data_len = r.read_checked_length();
        if (data_len > kMaxAssetBytes) {
            throw SpudpackError(
                "spudpack asset exceeds 256MiB cap", data_len_at);
        }
        if (!asset.path.empty() && asset.path.back() == '/' && data_len != 0) {
            throw SpudpackError(
                "spudpack empty-leaf path carries data", path_at);
        }
        r.read_bytes_into(asset.data, data_len);

        pack.assets.push_back(std::move(asset));
    }

    std::size_t dep_at = r.offset();
    std::uint64_t dep_count = r.read_varint();
    if (dep_count != 0) {
        throw SpudpackError("spudpack v1 does not support deps", dep_at);
    }

    if (r.remaining() != 0) {
        throw SpudpackError("spudpack has trailing bytes", r.offset());
    }

    std::uint32_t want_crc = crc32(data, size - 4);
    std::uint32_t got_crc = static_cast<std::uint32_t>(data[size - 4]) |
                            static_cast<std::uint32_t>(data[size - 3]) << 8 |
                            static_cast<std::uint32_t>(data[size - 2]) << 16 |
                            static_cast<std::uint32_t>(data[size - 1]) << 24;
    if (want_crc != got_crc) {
        throw SpudpackError("spudpack CRC mismatch", size - 4);
    }

    return pack;
}

void spudpack_write_file(const std::filesystem::path&, const Spudpack&) {
    throw SpudpackError("spudpack_write_file not yet implemented");
}

Spudpack spudpack_read_file(const std::filesystem::path&) {
    throw SpudpackError("spudpack_read_file not yet implemented");
}

}  // namespace spudplate
