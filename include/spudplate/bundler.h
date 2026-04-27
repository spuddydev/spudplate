#ifndef SPUDPLATE_BUNDLER_H
#define SPUDPLATE_BUNDLER_H

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "spudplate/ast.h"
#include "spudplate/spudpack.h"

namespace spudplate {

/**
 * @brief The set of assets a parsed program references on disk.
 *
 * Asset paths are normalised (forward-slash separators, no leading `/`,
 * no `.` or `..` segments, no embedded NUL) and deduped by path — entries
 * with the same normalised path are collapsed iff their bytes and mode
 * match. A trailing `/` on a path means "empty leaf directory" and the
 * data field is empty.
 */
struct BundleResult {
    std::vector<SpudpackAsset> assets;  ///< Deduped, normalised assets ready to embed in a spudpack.
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
 * @brief Walk a parsed program and collect every asset it references.
 *
 * `source_root` is the directory the source `.spud` lives in — relative
 * source paths in `file ... from`, `mkdir ... from`, and `copy` resolve
 * against it. The bundler dereferences symlinks, breaks loops on
 * canonical directory paths, and rejects:
 *   - source paths whose first segment is dynamic (interpolation or alias)
 *   - dynamic segments that splice mid-filename
 *   - `copy` sources that resolve to a regular file
 *   - non-regular non-directory file types (fifo, socket, block, char)
 *   - entries whose canonical target falls outside canonical source_root
 *   - duplicates with the same normalised path but conflicting bytes or
 *     mode
 */
BundleResult bundle_assets(const Program& program,
                           const std::filesystem::path& source_root);

}  // namespace spudplate

#endif  // SPUDPLATE_BUNDLER_H
