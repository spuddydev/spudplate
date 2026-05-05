#ifndef SPUDPLATE_BUNDLER_H
#define SPUDPLATE_BUNDLER_H

#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "spudplate/ast.h"
#include "spudplate/spudpack.h"

namespace spudplate {

/**
 * @brief The set of assets and dependencies a parsed program references.
 *
 * Asset paths are normalised (forward-slash separators, no leading `/`,
 * no `.` or `..` segments, no embedded NUL) and deduped by path - entries
 * with the same normalised path are collapsed iff their bytes and mode
 * match. A trailing `/` on a path means "empty leaf directory" and the
 * data field is empty.
 *
 * Deps are the bytes of every installed `.spp` referenced by an `include`
 * statement, deduped by name. Deps appear in source-order of their first
 * include site so error messages for cross-dep clashes name the earliest
 * point where a name became live.
 */
struct BundleResult {
    std::vector<SpudpackAsset> assets;  ///< Deduped, normalised assets ready to embed in a spudpack.
    std::vector<SpudpackDep> deps;      ///< Deduped, name-keyed dependencies ready to embed in a spudpack.
};

/**
 * @brief Raised when a `from`/`copy` source path cannot be bundled.
 *
 * Carries the source line and column of the offending statement so the
 * CLI can point a template author at the exact line.
 */
class BundleError : public std::runtime_error {
  public:
    /** @brief Construct with a message and the source line/column of the offending statement. */
    BundleError(std::string message, int line, int column);
    /** @brief Source line of the offending statement (1-based). */
    int line() const noexcept { return line_; }
    /** @brief Source column of the offending statement (1-based). */
    int column() const noexcept { return column_; }

  private:
    int line_;
    int column_;
};

/**
 * @brief Optional inputs shaping how `bundle_assets` resolves `include` deps.
 *
 * `existing_parent`, when non-null, supplies the previously-installed
 * parent's bundled deps so the bundler can default to "sticky" - reusing
 * the bytes the parent already carried instead of re-fetching from the
 * install root. This preserves the author's choice of dep version across
 * source-only edits to the parent.
 *
 * `update_deps`, when non-null, names the unpinned deps the caller wants
 * refreshed from the current install root despite sticky default. Pinned
 * deps (those with `version_pin` in source) are unaffected by this set;
 * resolution always honours the pin.
 */
struct BundleOptions {
    /// Previously-installed parent whose bundled deps the bundler should
    /// reuse by default ("sticky" mode). Null when there is no prior install.
    const Spudpack* existing_parent = nullptr;
    /// Names of unpinned deps the caller wants refreshed from the install
    /// root despite the sticky default. Null means "no overrides".
    const std::unordered_set<std::string>* update_deps = nullptr;
};

/**
 * @brief Notes the bundler emits as side outputs during a run.
 *
 * Today it carries the names of pinned deps that were also listed in
 * `--update-deps`, so the CLI can print the "pinned, --update-deps
 * ignored" hint without the bundler having to take a stream parameter.
 */
struct BundleNotes {
    /// Names of deps that were listed in `--update-deps` but are pinned in
    /// source; the bundler honoured the pin and ignored the override.
    std::vector<std::string> ignored_update_pins;
};

/**
 * @brief Walk a parsed program and collect every asset and dependency it references.
 *
 * `source_root` is the directory the source `.spud` lives in - relative
 * source paths in `file ... from`, `mkdir ... from`, and `copy` resolve
 * against it.
 *
 * `install_root` is the directory installed templates live in. Each
 * `include <name>` statement resolves to `<install_root>/<name>.spp` and
 * the file's bytes are attached as a dep. An empty `install_root` means
 * the caller is not in install mode and any `include` statement is a
 * `BundleError`.
 *
 * `options` carries the existing parent (for sticky default) and the
 * `--update-deps` set. See `BundleOptions`.
 *
 * `notes`, when non-null, receives advisory output (e.g. pinned deps
 * listed in `--update-deps`) the caller may wish to relay.
 *
 * Dep resolution rules:
 *   - Pinned `include foo@N`: read `<install_root>/foo.spp` if it has
 *     `version_tag == N`; otherwise read `<install_root>/.archive/foo.vN.spp`.
 *     If neither carries N, raise `BundleError`.
 *   - Unpinned `include foo`: if the existing parent carries `foo`, reuse
 *     its bundled bytes (sticky default). If `update_deps` lists `foo`,
 *     refresh from `<install_root>/foo.spp` instead. First install of the
 *     parent (no existing parent) always reads from the install root.
 *
 * The bundler dereferences symlinks, breaks loops on canonical directory
 * paths, and rejects:
 *   - source paths whose first segment is dynamic (interpolation or alias)
 *   - dynamic segments that splice mid-filename
 *   - `copy` sources that resolve to a regular file
 *   - non-regular non-directory file types (fifo, socket, block, char)
 *   - entries whose canonical target falls outside canonical source_root
 *   - duplicates with the same normalised path but conflicting bytes or
 *     mode
 *   - `include` whose name is not installed under `install_root`
 *   - `include` whose resolved file is not a regular file or fails to
 *     decode as a valid spudpack
 */
BundleResult bundle_assets(const Program& program,
                           const std::filesystem::path& source_root,
                           const std::filesystem::path& install_root = {},
                           const BundleOptions& options = {},
                           BundleNotes* notes = nullptr);

}  // namespace spudplate

#endif  // SPUDPLATE_BUNDLER_H
