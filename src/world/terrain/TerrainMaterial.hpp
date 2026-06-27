#pragma once

#include "content/biomes/BiomeData.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>

namespace terrain::terrain {

/// Terrain materials used by the smooth surface renderer.
///
/// These intentionally model natural ground only. The legacy block layer keeps
/// handling placed blocks, fluids, and other discrete gameplay geometry.
enum class MaterialId : uint8_t {
  Grass = 0,
  Dirt  = 1,
  Stone = 2,
  Sand  = 3,
  Gravel = 4,
  Clay = 5,
  CrackedStone = 6,
};

/// Material selection result for a terrain vertex.
///
/// The renderer can treat `primary` as the dominant material and mix in
/// `secondary` by `blend` near biome or slope transitions.
struct TerrainMaterial {
  MaterialId primary = MaterialId::Stone;
  MaterialId secondary = MaterialId::Stone;
  float blend = 0.0f;
  float tint = 0.0f;

  [[nodiscard]] constexpr auto dominant() const -> MaterialId {
    return blend >= 0.5f ? secondary : primary;
  }

  [[nodiscard]] constexpr operator MaterialId() const {
    return dominant();
  }
};

/// Inputs used to resolve a terrain material at a surface point.
struct TerrainMaterialContext {
  float worldX = 0.0f;
  float worldY = 0.0f;
  float worldZ = 0.0f;
  float surfaceHeight = 0.0f;
  float seaLevel = 0.0f;
  float depthBelowSurface = 0.0f;
  float slope = 0.0f; // 0 = flat, 1 = vertical
  float temperature = 0.0f;
  float humidity = 0.0f;
  biome::BiomeId biomeId = biome::BiomeId::Plains;
  int32_t surfaceDepth = 4;
  bool noWater = false;
};

[[nodiscard]] inline auto resolveTerrainMaterial(const TerrainMaterialContext& ctx) -> TerrainMaterial {
  TerrainMaterial result{};

  const float slope = std::clamp(ctx.slope, 0.0f, 1.0f);
  const float depth = std::max(0.0f, ctx.depthBelowSurface);
  const float surfaceDepth = static_cast<float>(std::max(1, ctx.surfaceDepth));
  const float depthRatio = std::clamp(depth / surfaceDepth, 0.0f, 1.0f);
  const float grassTint = std::clamp(0.40f + ctx.humidity * 0.45f - ctx.temperature * 0.20f, 0.0f, 1.0f);

  // Generate low-frequency and high-frequency noise patches based on world coordinates
  const float patchNoise = std::clamp(std::sin(ctx.worldX * 0.15f) * std::sin(ctx.worldZ * 0.15f) * 0.5f + 0.5f, 0.0f, 1.0f);
  const float detailNoise = std::clamp(std::sin(ctx.worldX * 1.7f) * std::sin(ctx.worldY * 1.7f) * std::sin(ctx.worldZ * 1.7f) * 0.5f + 0.5f, 0.0f, 1.0f);

  const float slopeToStone = biome::smoothEdge(slope, 0.35f, 0.45f);
  const float shallowDepth = biome::smoothEdge(depth, 0.80f, 1.50f);
  const float waterDistance = std::abs(ctx.surfaceHeight - ctx.seaLevel);

  float sandWeight = ctx.noWater ? 0.0f : 1.0f - biome::smoothEdge(waterDistance, 2.0f, 4.0f);
  if (ctx.biomeId == biome::BiomeId::Desert || ctx.biomeId == biome::BiomeId::Ocean) {
    sandWeight = std::max(sandWeight, 0.85f);
  }

  if (sandWeight > 0.45f) {
    if (ctx.surfaceHeight < ctx.seaLevel - 2.0f && ctx.biomeId == biome::BiomeId::Ocean) {
        result.primary = MaterialId::Clay;
    } else {
        result.primary = MaterialId::Sand;
    }
    result.secondary = MaterialId::Stone;
    const float sandDepthBlend =
        std::clamp(depth / std::max(1.0f, surfaceDepth * 1.5f), 0.0f, 1.0f);
    result.blend = std::clamp(std::max(slopeToStone * 0.75f, sandDepthBlend), 0.0f, 1.0f);
    result.tint = 0.15f;
    return result;
  }

  if (depth <= 0.85f) {
    result.primary = MaterialId::Grass;
    result.secondary = (slopeToStone > 0.40f) ? MaterialId::Stone : MaterialId::Dirt;
    result.blend = std::clamp(std::max(slopeToStone, shallowDepth * 0.35f), 0.0f, 1.0f);
    
    // Vary the grass tint slightly using the patch noise for a more natural look
    const float tintVariation = (patchNoise - 0.5f) * 0.3f;
    result.tint = std::clamp(grassTint + tintVariation, 0.0f, 1.0f);
    
    // Mountain peaks or steep slopes might use gravel/cracked stone
    if (ctx.biomeId == biome::BiomeId::Mountains && slopeToStone > 0.6f) {
        result.primary = MaterialId::Gravel;
        result.secondary = MaterialId::CrackedStone;
        result.blend = biome::smoothEdge(slope, 0.65f, 0.85f);
    }
    
    return result;
  }

  if (depth < surfaceDepth) {
    result.primary = MaterialId::Dirt;
    result.secondary = MaterialId::Gravel;
    
    // Mix some gravel patches into the dirt based on depth and noise
    const float gravelPatch = biome::smoothEdge(patchNoise + detailNoise * 0.4f, 0.7f, 1.0f);
    result.blend = std::clamp(depthRatio * 0.5f + gravelPatch * 0.8f, 0.0f, 1.0f);
    result.tint = 0.0f;
    return result;
  }

  result.primary = MaterialId::Stone;
  result.secondary = MaterialId::CrackedStone;
  
  // Blend cracked stone based on steep slopes OR random noise patches
  const float baseBlend = biome::smoothEdge(slope, 0.6f, 0.8f);
  const float crackedPatch = biome::smoothEdge(patchNoise + detailNoise * 0.2f, 0.65f, 0.85f);
  result.blend = std::clamp(std::max(baseBlend, crackedPatch), 0.0f, 1.0f);
  result.tint = 0.0f;
  return result;
}

} // namespace terrain::terrain
