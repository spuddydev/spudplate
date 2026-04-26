#ifndef SPUDPLATE_ASSET_BUNDLER_H
#define SPUDPLATE_ASSET_BUNDLER_H

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "spudplate/ast.h"

namespace spudplate {

/**
 * @brief Thrown when the bundler cannot resolve an asset reference at install
 * time.
 *
 * Carries the offending node's source position so callers can prefix the
 * usual `<file>:<line>:<col>:` diagnostic.
 */
class BundleError : public std::runtime_error {
  public:
    BundleError(const std::string& message, int line, int column)
        : std::runtime_error(message), line_(line), column_(column) {}

    [[nodiscard]] int line() const { return line_; }
    [[nodiscard]] int column() const { return column_; }

  private:
    int line_;
    int column_;
};

/**
 * @brief Summary of work performed by `bundle_assets`.
 *
 * `copied_files` lists every regular file written under `<dest>/assets/`,
 * relative to `dest`. Useful for tests and for surfacing what landed in the
 * spudpack to the user.
 */
struct BundleReport {
    std::vector<std::filesystem::path> copied_files;
};

/**
 * @brief Walk `program`, locate every `from`/`copy` source path, copy the
 * referenced files into `<dest>/assets/<...>` preserving directory structure.
 *
 * @param program     Parsed and validated program.
 * @param source_root Directory the `.spud` source lived in; relative `from`
 *                    paths resolve against this.
 * @param dest        The spudpack directory (assets are written under
 *                    `dest/assets/`).
 *
 * Each asset reference is classified by the static prefix of its `PathExpr`:
 *
 * - All segments literal — bundle that exact file or directory recursively.
 * - Literal prefix followed by `{var}` or `{expr}` — bundle the deepest
 *   directory the literal prefix points into.
 * - First segment dynamic, or no `/` in the literal prefix — `BundleError`
 *   ("asset path has no static prefix; templates installed via spudpack
 *   must root assets under a literal path").
 *
 * Throws `BundleError` on a missing source file, on path-traversal attempts,
 * or on paths with no static prefix. The caller is responsible for cleaning
 * up `dest` if a partial bundle was written before the throw.
 */
BundleReport bundle_assets(const Program& program,
                           const std::filesystem::path& source_root,
                           const std::filesystem::path& dest);

}  // namespace spudplate

#endif  // SPUDPLATE_ASSET_BUNDLER_H
