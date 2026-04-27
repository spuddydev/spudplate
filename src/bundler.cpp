#include "spudplate/bundler.h"

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

BundleResult bundle_assets(const Program&, const fs::path&) {
    throw BundleError("bundle_assets not yet implemented", 0, 0);
}

}  // namespace spudplate
