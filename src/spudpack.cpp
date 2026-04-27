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

std::vector<std::uint8_t> spudpack_encode(const Spudpack&) {
    throw SpudpackError("spudpack_encode not yet implemented");
}

Spudpack spudpack_decode(const std::uint8_t*, std::size_t) {
    throw SpudpackError("spudpack_decode not yet implemented");
}

void spudpack_write_file(const std::filesystem::path&, const Spudpack&) {
    throw SpudpackError("spudpack_write_file not yet implemented");
}

Spudpack spudpack_read_file(const std::filesystem::path&) {
    throw SpudpackError("spudpack_read_file not yet implemented");
}

}  // namespace spudplate
