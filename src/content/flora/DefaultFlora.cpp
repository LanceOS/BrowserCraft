#include "DefaultFlora.hpp"
#include "FloraRegistry.hpp"
#include "GrassFactory.hpp"
#include "FloraTypes.hpp"
#include "world/BlockIds.hpp"

namespace voxel::flora {

std::unique_ptr<FloraRegistry> createDefaultFloraRegistry() {
  auto registry = std::make_unique<FloraRegistry>();

  // --- Grass ---
  // Block definitions are in blocks.json and registered by VanillaBlockFactory.
  // The GrassFactory handles metadata only (render type, soil affinity, etc.).
  // Texture layer is 0 here because the block's texture comes from JSON registration.
  registry->registerCategory(std::make_unique<GrassFactory>(std::vector<GrassSpeciesDefinition>{
    {
      .speciesId = 0,
      .name = "Tall Grass",
      .blockId = BlockId::TALL_GRASS,
      .textureLayer = 0,
      .acceptableSoil = {SoilType::GRASS},
      .biomeAffinity = {"plains", "forest"},
      .spawnChance = 0.1f,
      .isTall = false,
      .dropsSeeds = true,
      .seedItemId = BlockId::TALL_GRASS,
    },
    {
      .speciesId = 1,
      .name = "Fern",
      .blockId = BlockId::FERN,
      .textureLayer = 0,
      .acceptableSoil = {SoilType::GRASS},
      .biomeAffinity = {"forest", "swamp"},
      .spawnChance = 0.05f,
      .isTall = false,
      .dropsSeeds = false,
      .seedItemId = 0,
    },
    {
      .speciesId = 2,
      .name = "Double Tallgrass",
      .blockId = BlockId::TALL_GRASS,  // Shares block ID with tall grass; placed as 2-block stack
      .textureLayer = 0,
      .acceptableSoil = {SoilType::GRASS},
      .biomeAffinity = {"plains"},
      .spawnChance = 0.03f,
      .isTall = true,
      .dropsSeeds = true,
      .seedItemId = BlockId::TALL_GRASS,
    },
  }));

  return registry;
}

} // namespace voxel::flora
