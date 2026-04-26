#include "spudplate/asset_bundler.h"

#include <stdexcept>
#include <string>
#include <variant>

namespace spudplate {

namespace {

// Result of pulling a leading literal run off a `PathExpr`. The bundler
// either copies an exact file/directory (fully_static), the deepest static
// directory the path roots into (prefix_dir), or refuses with an error
// (no_prefix).
enum class PrefixKind { FullyStatic, PrefixDir, NoPrefix };

struct StaticPrefix {
    PrefixKind kind;
    std::string value;  ///< Concatenated literal text, with no trailing `/` trimmed.
};

// Walk `segments` collecting leading `PathLiteral::value` text. Stops at the
// first non-literal segment.
//
// Classification:
//   - All segments literal → FullyStatic, value = full path text.
//   - Some literals then a non-literal segment → PrefixDir, value trimmed at
//     the last `/` (so `base/{x}/main.cpp` yields `base`). If no `/` in the
//     literal run (e.g. `foo{x}.cpp`), classify as NoPrefix — the dynamic
//     part is mid-filename and the bundler can't choose a directory to walk.
//   - First segment is non-literal (or no segments) → NoPrefix.
StaticPrefix classify_path(const PathExpr& path) {
    std::string literal;
    bool saw_dynamic = false;
    for (const auto& seg : path.segments) {
        if (std::holds_alternative<PathLiteral>(seg)) {
            if (saw_dynamic) {
                // Mixed run: trailing literals after a dynamic segment don't
                // extend the static prefix, just stop counting.
                break;
            }
            literal += std::get<PathLiteral>(seg).value;
        } else {
            saw_dynamic = true;
            break;
        }
    }
    if (literal.empty()) {
        return {PrefixKind::NoPrefix, {}};
    }
    if (!saw_dynamic) {
        return {PrefixKind::FullyStatic, literal};
    }
    auto last_slash = literal.find_last_of('/');
    if (last_slash == std::string::npos) {
        return {PrefixKind::NoPrefix, {}};
    }
    return {PrefixKind::PrefixDir, literal.substr(0, last_slash)};
}

}  // namespace

BundleReport bundle_assets(const Program& /*program*/,
                           const std::filesystem::path& /*source_root*/,
                           const std::filesystem::path& /*dest*/) {
    throw std::logic_error("bundle_assets: not implemented yet");
}

}  // namespace spudplate
