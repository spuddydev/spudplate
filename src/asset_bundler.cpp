#include "spudplate/asset_bundler.h"

#include <stdexcept>

namespace spudplate {

BundleReport bundle_assets(const Program& /*program*/,
                           const std::filesystem::path& /*source_root*/,
                           const std::filesystem::path& /*dest*/) {
    throw std::logic_error("bundle_assets: not implemented yet");
}

}  // namespace spudplate
