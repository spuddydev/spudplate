#include "spudplate/bundler.h"

#include <cstdio>
#include <fstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace spudplate {

namespace fs = std::filesystem;

BundleError::BundleError(std::string message, int line, int column)
    : std::runtime_error(std::move(message)), line_(line), column_(column) {}

namespace {

// Result of classifying a source PathExpr. The bundler distinguishes the
// two legal shapes - a fully-literal path (walk the exact target) and a
// path whose leading run of literal segments is followed by one or more
// dynamic segments (walk the static-prefix directory). The split-on-`/`
// parser keeps separators out of segment values, so the prefix is built by
// joining leading PathLiterals with `/` and appending a trailing `/` once
// a dynamic segment is encountered.
struct ClassifiedSource {
    enum class Shape {
        Static,        ///< Every segment is a PathLiteral.
        StaticPrefix,  ///< Some segments are dynamic; the literal prefix
                       ///< covers entire path components and ends with `/`.
    };
    Shape shape;
    std::string literal_prefix;  ///< Joined leading PathLiteral values.
};

ClassifiedSource classify_source_path(const PathExpr& path) {
    if (path.segments.empty()) {
        throw BundleError("source path is empty", path.line, path.column);
    }
    if (!std::holds_alternative<PathLiteral>(path.segments.front())) {
        throw BundleError(
            "source path may not start with a dynamic segment",
            path.line, path.column);
    }

    std::string literal;
    bool has_dynamic = false;
    for (const auto& seg : path.segments) {
        if (std::holds_alternative<PathLiteral>(seg)) {
            if (!literal.empty()) {
                literal.push_back('/');
            }
            literal.append(std::get<PathLiteral>(seg).value);
        } else {
            has_dynamic = true;
            break;
        }
    }

    if (!has_dynamic) {
        return {ClassifiedSource::Shape::Static, std::move(literal)};
    }
    // Append the trailing `/` that separates the static prefix from the
    // first dynamic segment. The split-on-`/` parser keeps separators out
    // of segment values, so we synthesise the boundary here.
    literal.push_back('/');
    return {ClassifiedSource::Shape::StaticPrefix, std::move(literal)};
}

// Read a regular file's bytes off disk. Errors here are unusual - the
// walker has already established the entry is a regular file - so they
// surface as a BundleError pointing at the originating statement.
std::vector<std::uint8_t> read_file_bytes(const fs::path& p, int line, int column) {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        throw BundleError("could not read source: " + p.string(), line, column);
    }
    in.seekg(0, std::ios::end);
    auto end = in.tellg();
    if (end < 0) {
        throw BundleError("could not size source: " + p.string(), line, column);
    }
    std::vector<std::uint8_t> out(static_cast<std::size_t>(end));
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(out.data()),
            static_cast<std::streamsize>(out.size()));
    if (in.gcount() != static_cast<std::streamsize>(out.size())) {
        throw BundleError("short read on source: " + p.string(), line, column);
    }
    return out;
}

// File-permission bits on disk include setuid/setgid/sticky which spudplate
// templates have no business carrying. Mask down to the standard 9 user/
// group/other bits so a template author cannot quietly ship a setuid
// payload, and so the codec's `mode & ~0o7777 == 0` check is satisfied
// trivially on every producer-side asset.
std::uint16_t disk_mode_to_asset_mode(fs::perms p) {
    return static_cast<std::uint16_t>(static_cast<unsigned>(p) & 0777);
}

class Bundler {
  public:
    Bundler(const fs::path& source_root, const fs::path& install_root)
        : root_(fs::weakly_canonical(source_root)),
          install_root_(install_root) {}

    BundleResult run(const Program& program) {
        for (const auto& stmt : program.statements) {
            if (stmt) visit_stmt(*stmt);
        }
        BundleResult out;
        out.assets.reserve(by_path_.size());
        for (const auto& key : insertion_order_) {
            out.assets.push_back(std::move(by_path_[key]));
        }
        out.deps.reserve(dep_order_.size());
        for (const auto& name : dep_order_) {
            out.deps.push_back(std::move(deps_[name]));
        }
        return out;
    }

  private:
    void visit_stmt(const Stmt& stmt) {
        std::visit(
            [&](const auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, FileStmt>) {
                    if (std::holds_alternative<FileFromSource>(s.source)) {
                        const auto& src = std::get<FileFromSource>(s.source);
                        process_file_source(src.path, s.line, s.column);
                    }
                } else if constexpr (std::is_same_v<T, MkdirStmt>) {
                    if (s.from_source.has_value()) {
                        process_dir_source(*s.from_source, s.line, s.column,
                                           /*forbid_regular_file=*/false);
                    }
                } else if constexpr (std::is_same_v<T, CopyStmt>) {
                    process_dir_source(s.source, s.line, s.column,
                                       /*forbid_regular_file=*/true);
                } else if constexpr (std::is_same_v<T, RepeatStmt>) {
                    for (const auto& body : s.body) {
                        if (body) visit_stmt(*body);
                    }
                } else if constexpr (std::is_same_v<T, IfStmt>) {
                    for (const auto& body : s.body) {
                        if (body) visit_stmt(*body);
                    }
                } else if constexpr (std::is_same_v<T, IncludeStmt>) {
                    process_include(s.name, s.line, s.column);
                }
                // AskStmt, LetStmt, AssignStmt, RunStmt carry no asset or
                // dep references and are intentionally skipped.
            },
            stmt.data);
    }

    // Resolve `include <name>` against the install root, read the bundled
    // `.spp` bytes, and attach them as a dep. Multiple includes of the
    // same name dedupe to one record; the first encounter wins on order.
    void process_include(const std::string& name, int line, int column) {
        if (deps_.find(name) != deps_.end()) {
            return;  // already collected
        }
        if (install_root_.empty()) {
            throw BundleError(
                "include '" + name +
                    "' has no install root to resolve against",
                line, column);
        }
        if (name.empty() || name.find('/') != std::string::npos ||
            name.find('\0') != std::string::npos || name == "." ||
            name == "..") {
            throw BundleError(
                "include name is not a bare identifier: '" + name + "'",
                line, column);
        }
        fs::path dep_path = install_root_ / (name + ".spp");
        std::error_code ec;
        if (!fs::exists(dep_path, ec) || ec) {
            throw BundleError(
                "include '" + name + "' is not installed at " +
                    dep_path.string(),
                line, column);
        }
        if (!fs::is_regular_file(dep_path, ec) || ec) {
            throw BundleError(
                "include '" + name + "' is not a regular file at " +
                    dep_path.string(),
                line, column);
        }
        std::vector<std::uint8_t> bytes = read_file_bytes(dep_path, line, column);
        try {
            spudpack_decode(bytes.data(), bytes.size());
        } catch (const SpudpackError& e) {
            throw BundleError(
                "include '" + name + "' is not a valid spudpack: " + e.what(),
                line, column);
        }
        SpudpackDep dep;
        dep.name = name;
        dep.bytes = std::move(bytes);
        deps_.emplace(name, std::move(dep));
        dep_order_.push_back(name);
    }

    // `file ... from <path>` - the path may resolve to a regular file
    // (whole-file embed) or to a directory whose contents are walked. The
    // static-prefix-with-dynamic-suffix form means "the static prefix is a
    // directory, and the runtime resolution picks one of the bundled files
    // inside it" - same effect on bundling as the directory case.
    void process_file_source(const PathExpr& path, int line, int column) {
        auto cls = classify_source_path(path);
        fs::path target = resolve_under_root(cls.literal_prefix, line, column);

        if (cls.shape == ClassifiedSource::Shape::StaticPrefix) {
            walk_dir_into_assets(target, cls.literal_prefix, line, column);
            return;
        }
        std::error_code ec;
        auto status = fs::symlink_status(target, ec);
        if (ec || !fs::exists(status)) {
            throw BundleError("source not found: " + target.string(), line, column);
        }
        auto resolved = fs::status(target, ec);
        if (fs::is_regular_file(resolved)) {
            record_file(target, cls.literal_prefix, line, column);
        } else if (fs::is_directory(resolved)) {
            walk_dir_into_assets(target, cls.literal_prefix, line, column);
        } else {
            throw BundleError("unsupported file type at " + target.string(),
                              line, column);
        }
    }

    // `mkdir ... from <path>` and `copy <path> into ...` - both must
    // resolve to a directory. CopyStmt explicitly disallows a regular-file
    // source per the language spec.
    void process_dir_source(const PathExpr& path, int line, int column,
                            bool forbid_regular_file) {
        auto cls = classify_source_path(path);
        fs::path target = resolve_under_root(cls.literal_prefix, line, column);

        std::error_code ec;
        auto resolved = fs::status(target, ec);
        if (ec || !fs::exists(resolved)) {
            throw BundleError("source not found: " + target.string(), line, column);
        }
        if (fs::is_regular_file(resolved)) {
            if (forbid_regular_file) {
                throw BundleError(
                    "copy source must be a directory: " + target.string(),
                    line, column);
            }
            // mkdir from <regular file> is a parser-rejected form in
            // theory; treat it defensively the same way as CopyStmt.
            throw BundleError(
                "mkdir source must be a directory: " + target.string(),
                line, column);
        }
        if (!fs::is_directory(resolved)) {
            throw BundleError("unsupported file type at " + target.string(),
                              line, column);
        }
        walk_dir_into_assets(target, cls.literal_prefix, line, column);
    }

    // Compose `<source_root>/<literal_prefix>` and verify the canonical
    // form does not escape canonical source_root. Symlinks in the prefix
    // that point outside the root are rejected here.
    fs::path resolve_under_root(const std::string& literal_prefix, int line,
                                int column) {
        fs::path joined = root_ / literal_prefix;
        std::error_code ec;
        fs::path canon = fs::weakly_canonical(joined, ec);
        if (ec) {
            throw BundleError("could not resolve source: " + joined.string(),
                              line, column);
        }
        if (!is_under(canon, root_)) {
            throw BundleError("source path escapes source root", line, column);
        }
        return canon;
    }

    static bool is_under(const fs::path& candidate, const fs::path& root) {
        auto cit = candidate.begin();
        auto rit = root.begin();
        for (; rit != root.end() && cit != candidate.end(); ++rit, ++cit) {
            if (*cit != *rit) return false;
        }
        return rit == root.end();
    }

    // Walk a directory whose root corresponds to the asset-key prefix
    // `key_prefix`. Each regular file becomes an asset under
    // `key_prefix + relative_path`. Directories that turn out to be
    // empty leaves are recorded with a trailing `/`.
    void walk_dir_into_assets(const fs::path& dir, const std::string& raw_prefix,
                              int line, int column) {
        // The static-prefix classifier returns prefixes that already end
        // with `/`, but the all-literal classifier returns the raw path
        // (e.g. `src` for `from src`). Normalise once here so every
        // downstream concatenation produces a well-formed key.
        std::string key_prefix = raw_prefix;
        if (!key_prefix.empty() && key_prefix.back() != '/') {
            key_prefix.push_back('/');
        }

        std::error_code ec;
        std::unordered_set<std::string> visited;

        // Track which directories we've descended into so a symlink that
        // points back at a parent does not trigger infinite recursion.
        // recursive_directory_iterator with follow_directory_symlink does
        // the *following* but does not break loops.
        std::unordered_map<std::string, bool> dir_has_children;

        fs::recursive_directory_iterator it(
            dir, fs::directory_options::follow_directory_symlink, ec);
        if (ec) {
            throw BundleError(
                "could not walk source: " + dir.string(), line, column);
        }
        fs::recursive_directory_iterator end;

        while (it != end) {
            const fs::directory_entry& entry = *it;
            const fs::path& p = entry.path();

            // Source-root escape: a symlink can drop us outside the root.
            // Check every entry, not just directories.
            fs::path canon = fs::weakly_canonical(p, ec);
            if (ec || !is_under(canon, root_)) {
                throw BundleError("source path escapes source root", line, column);
            }

            // `fs::relative` calls `weakly_canonical` on both arguments,
            // which silently dereferences symlinks. Use `lexically_relative`
            // for the asset-key computation so a file symlink keeps its
            // bundled-tree position instead of being relocated to its
            // target. Symlink loops and source-root escape are caught
            // separately above.
            std::string rel = p.lexically_relative(dir).generic_string();
            std::string key = key_prefix + rel;

            auto status = entry.symlink_status(ec);
            // Resolve through symlinks for the type decision so a symlink
            // to a regular file is bundled as a regular file.
            auto resolved = entry.status(ec);

            if (fs::is_directory(resolved)) {
                std::string canon_str = canon.string();
                if (visited.count(canon_str)) {
                    it.disable_recursion_pending();
                    it.increment(ec);
                    continue;
                }
                visited.insert(canon_str);
                dir_has_children[normalise_dir_key(key)] = false;
                // Mark every ancestor key as having a child so it is not
                // recorded as an empty leaf.
                mark_ancestors(key_prefix, key, dir_has_children);
                it.increment(ec);
                continue;
            }

            if (fs::is_regular_file(resolved)) {
                // Mark ancestors so an enclosing directory with files is
                // not falsely flagged as an empty leaf.
                mark_ancestors(key_prefix, key, dir_has_children);
                record_file(p, key, line, column);
                it.increment(ec);
                continue;
            }

            throw BundleError("unsupported file type at " + p.string(),
                              line, column);
        }

        // After the walk, record a trailing-slash entry for every directory
        // that has no children at all (no files anywhere underneath, no
        // recorded sub-dirs of its own).
        for (const auto& [dir_key, has_child] : dir_has_children) {
            if (has_child) continue;
            // The walker keys directories with a trailing slash already.
            // Read the on-disk mode for the directory itself.
            fs::path on_disk = dir / fs::path(dir_key.substr(key_prefix.size()));
            std::error_code ec_mode;
            auto perms = fs::status(on_disk, ec_mode).permissions();
            std::uint16_t mode = ec_mode ? std::uint16_t{0755}
                                         : disk_mode_to_asset_mode(perms);
            insert_or_collide(dir_key, mode, /*data=*/{}, line, column);
        }
    }

    // Trailing slash means "directory" in the asset key space.
    static std::string normalise_dir_key(std::string key) {
        if (!key.empty() && key.back() != '/') key.push_back('/');
        return key;
    }

    // For an asset key like `prefix/sub/leaf.txt`, mark every prefix
    // directory `prefix/sub/` as having a child so the empty-leaf scan
    // skips it. We only walk back as far as `key_prefix`.
    void mark_ancestors(const std::string& key_prefix, const std::string& key,
                        std::unordered_map<std::string, bool>& has_children) {
        std::size_t i = key.find('/', key_prefix.size());
        while (i != std::string::npos) {
            has_children[key.substr(0, i + 1)] = true;
            i = key.find('/', i + 1);
        }
    }

    // Read a regular file off disk, normalise its asset key, and insert
    // (or collide-and-error) into the dedup map.
    void record_file(const fs::path& p, const std::string& raw_key, int line,
                     int column) {
        auto bytes = read_file_bytes(p, line, column);
        std::error_code ec;
        auto perms = fs::status(p, ec).permissions();
        std::uint16_t mode = ec ? std::uint16_t{0644}
                                : disk_mode_to_asset_mode(perms);
        insert_or_collide(raw_key, mode, std::move(bytes), line, column);
    }

    // Insert a normalised asset record. If the key is already present the
    // bytes and mode must match - otherwise the bundler reports the more
    // specific of the two diagnostics so a template author can see whether
    // the conflict is content or permissions.
    void insert_or_collide(const std::string& raw_key, std::uint16_t mode,
                           std::vector<std::uint8_t> data, int line, int column) {
        std::string key;
        try {
            key = normalize_asset_path(raw_key);
        } catch (const SpudpackError& e) {
            throw BundleError(
                std::string("invalid asset path: ") + e.what(), line, column);
        }
        auto it = by_path_.find(key);
        if (it == by_path_.end()) {
            SpudpackAsset asset;
            asset.path = key;
            asset.mode = mode;
            asset.data = std::move(data);
            by_path_.emplace(key, std::move(asset));
            insertion_order_.push_back(key);
            return;
        }
        if (it->second.data != data) {
            throw BundleError("conflicting bytes for asset " + key, line, column);
        }
        if (it->second.mode != mode) {
            throw BundleError("conflicting mode for asset " + key, line, column);
        }
        // Identical record - fine; collapse silently.
    }

    fs::path root_;
    fs::path install_root_;
    std::unordered_map<std::string, SpudpackAsset> by_path_;
    std::vector<std::string> insertion_order_;
    std::unordered_map<std::string, SpudpackDep> deps_;
    std::vector<std::string> dep_order_;
};

}  // namespace

BundleResult bundle_assets(const Program& program, const fs::path& source_root,
                           const fs::path& install_root) {
    Bundler b(source_root, install_root);
    return b.run(program);
}

}  // namespace spudplate
