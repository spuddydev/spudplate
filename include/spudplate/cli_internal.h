#ifndef SPUDPLATE_CLI_INTERNAL_H
#define SPUDPLATE_CLI_INTERNAL_H

#include <filesystem>
#include <functional>

namespace spudplate::cli_internal {

/**
 * @brief Function that performs the final-step rename in `cmd_install`.
 *
 * `cmd_install` writes the spudpack to `<name>.spp.tmp` and then renames
 * it to `<name>.spp`. Tests need to inject a failure at the rename step
 * to exercise the cleanup path; production code calls
 * `std::filesystem::rename` directly. The seam is exposed here rather
 * than in the file-local anonymous namespace inside `src/cli.cpp` so an
 * extern reference from a test translation unit is well-formed.
 */
using RenameFn = std::function<void(const std::filesystem::path& from,
                                    const std::filesystem::path& to)>;

/**
 * @brief Mutable accessor for the rename function pointer.
 *
 * Returns a reference to a process-scope static. Tests swap the function
 * via an RAII guard that captures the previous value and restores it on
 * destruction.
 */
RenameFn& install_rename_fn();

}  // namespace spudplate::cli_internal

#endif  // SPUDPLATE_CLI_INTERNAL_H
