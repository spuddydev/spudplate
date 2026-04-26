#include "spudplate/asset_bundler.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace spudplate {

namespace {

// Result of pulling a leading literal run off a `PathExpr`. The bundler
// either copies an exact file/directory (FullyStatic), the deepest static
// directory the path roots into (PrefixDir), or refuses with an error
// (NoPrefix).
enum class PrefixKind { FullyStatic, PrefixDir, NoPrefix };

struct StaticPrefix {
    PrefixKind kind;
    std::string value;
};

// Walk `segments` collecting leading `PathLiteral::value` text. Stops at the
// first non-literal segment.
//
// Classification:
//   - All segments literal → FullyStatic, value = full path text.
//   - Some literals then a non-literal segment → PrefixDir, value trimmed at
//     the last `/`. If no `/` in the literal run (e.g. `foo{x}.cpp`),
//     classify as NoPrefix — the dynamic part is mid-filename and the
//     bundler can't choose a directory to walk.
//   - First segment is non-literal (or no segments) → NoPrefix.
StaticPrefix classify_path(const PathExpr& path) {
    std::string literal;
    bool saw_dynamic = false;
    for (const auto& seg : path.segments) {
        if (std::holds_alternative<PathLiteral>(seg)) {
            if (saw_dynamic) {
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

// One asset reference found in the program: the source path string (relative
// to `source_root`), the line/column of the offending node for diagnostics.
struct AssetRef {
    std::string relative;
    int line;
    int column;
};

void collect_paths(const std::vector<StmtPtr>& stmts,
                   std::vector<AssetRef>& out) {
    for (const auto& sp : stmts) {
        std::visit(
            [&](const auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, MkdirStmt>) {
                    if (s.from_source.has_value()) {
                        out.push_back({{}, s.from_source->line, s.from_source->column});
                        auto p = classify_path(*s.from_source);
                        out.back().relative = p.value;
                        out.back().line = s.from_source->line;
                        out.back().column = s.from_source->column;
                        if (p.kind == PrefixKind::NoPrefix) {
                            out.back().relative.clear();  // sentinel: rejected
                        }
                    }
                } else if constexpr (std::is_same_v<T, FileStmt>) {
                    if (std::holds_alternative<FileFromSource>(s.source)) {
                        const auto& fs = std::get<FileFromSource>(s.source);
                        auto p = classify_path(fs.path);
                        AssetRef ref{p.value, fs.path.line, fs.path.column};
                        if (p.kind == PrefixKind::NoPrefix) {
                            ref.relative.clear();
                        }
                        out.push_back(std::move(ref));
                    }
                } else if constexpr (std::is_same_v<T, CopyStmt>) {
                    auto p = classify_path(s.source);
                    AssetRef ref{p.value, s.source.line, s.source.column};
                    if (p.kind == PrefixKind::NoPrefix) {
                        ref.relative.clear();
                    }
                    out.push_back(std::move(ref));
                } else if constexpr (std::is_same_v<T, RepeatStmt>) {
                    collect_paths(s.body, out);
                }
            },
            sp->data);
    }
}

// Refuse `relative` if it escapes `source_root` once concatenated. Catches
// `from ../etc/passwd` and friends. We canonicalise the path expression
// against the source root and require the canonical form is still inside.
std::filesystem::path resolve_inside_root(
    const std::filesystem::path& source_root,
    const std::string& relative, int line, int column) {
    std::filesystem::path joined = source_root / relative;
    auto canonical = std::filesystem::weakly_canonical(joined);
    auto root_canonical = std::filesystem::weakly_canonical(source_root);
    auto rel = canonical.lexically_relative(root_canonical);
    auto rel_str = rel.generic_string();
    if (rel_str.empty() || rel_str == "." ||
        rel_str.rfind("..", 0) == 0) {
        throw BundleError(
            "asset path '" + relative + "' escapes the source directory",
            line, column);
    }
    return canonical;
}

void copy_recursive(const std::filesystem::path& src,
                    const std::filesystem::path& dest,
                    BundleReport& report,
                    const std::filesystem::path& dest_root) {
    namespace fs = std::filesystem;
    if (fs::is_regular_file(src)) {
        fs::create_directories(dest.parent_path());
        fs::copy_file(src, dest, fs::copy_options::overwrite_existing);
        report.copied_files.push_back(
            fs::relative(dest, dest_root));
        return;
    }
    if (!fs::is_directory(src)) {
        return;  // skip symlinks, devices, missing entries handled by caller
    }
    fs::create_directories(dest);
    for (const auto& entry : fs::recursive_directory_iterator(src)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto rel = fs::relative(entry.path(), src);
        auto out = dest / rel;
        fs::create_directories(out.parent_path());
        fs::copy_file(entry.path(), out, fs::copy_options::overwrite_existing);
        report.copied_files.push_back(fs::relative(out, dest_root));
    }
}

}  // namespace

BundleReport bundle_assets(const Program& program,
                           const std::filesystem::path& source_root,
                           const std::filesystem::path& dest) {
    namespace fs = std::filesystem;

    std::vector<AssetRef> refs;
    collect_paths(program.statements, refs);

    // First pass: surface rejection errors with source positions intact.
    for (const auto& ref : refs) {
        if (ref.relative.empty()) {
            throw BundleError(
                "asset path has no static prefix; templates installed via "
                "spudpack must root assets under a literal path",
                ref.line, ref.column);
        }
    }

    // Two-tier dedup. Sort relative paths so shorter prefixes come first;
    // skip any path that's already covered by a previously bundled prefix.
    std::sort(refs.begin(), refs.end(),
              [](const AssetRef& a, const AssetRef& b) {
                  return a.relative < b.relative;
              });

    BundleReport report;
    fs::path assets_root = dest / "assets";
    fs::create_directories(assets_root);

    std::vector<std::string> bundled_prefixes;
    std::set<fs::path> bundled_canonicals;

    for (const auto& ref : refs) {
        bool covered = false;
        for (const auto& prev : bundled_prefixes) {
            if (ref.relative == prev ||
                (ref.relative.size() > prev.size() &&
                 ref.relative.rfind(prev + "/", 0) == 0)) {
                covered = true;
                break;
            }
        }
        if (covered) {
            continue;
        }

        auto src = resolve_inside_root(source_root, ref.relative, ref.line,
                                       ref.column);
        if (!fs::exists(src)) {
            throw BundleError(
                "asset source '" + ref.relative + "' does not exist",
                ref.line, ref.column);
        }
        if (bundled_canonicals.count(src)) {
            continue;
        }
        bundled_canonicals.insert(src);

        auto out = assets_root / ref.relative;
        copy_recursive(src, out, report, dest);
        bundled_prefixes.push_back(ref.relative);
    }

    return report;
}

}  // namespace spudplate
