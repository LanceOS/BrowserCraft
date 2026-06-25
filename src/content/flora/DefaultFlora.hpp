#pragma once

#include <memory>

namespace voxel::flora {

class FloraRegistry;

/// Create the default flora registry with all standard flora categories.
/// Currently includes:
///   - Grass: Tall grass, ferns, double tallgrass
///
/// More categories (trees, flowers, crops) will be added as they are implemented.
[[nodiscard]] std::unique_ptr<FloraRegistry> createDefaultFloraRegistry();

} // namespace voxel::flora
