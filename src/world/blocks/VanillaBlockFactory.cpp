#include "VanillaBlockFactory.hpp"
#include "engine/assets/AssetManager.hpp"

namespace voxel {

void VanillaBlockFactory::registerAll(BlockRegistry& registry) {
  AssetManager::get().loadAssets();

  for (const auto& [id, def] : AssetManager::get().getBlockDefs()) {
    if (id == 0) continue; // Skip air — implicitly registered by registry capacity

    BlockDefinition bd;
    bd.id = def.id;
    bd.name = def.name;
    bd.textures.top = def.tex_top;
    bd.textures.bottom = def.tex_bottom;
    bd.textures.side = def.tex_side;
    bd.material.opaque = def.is_opaque;
    if (!def.is_opaque) {
      bd.material.transparent = true;
    }
    bd.material.liquid = def.is_liquid;
    bd.material.foliage = def.is_foliage;
    bd.material.lightEmission = def.light_emission;
    bd.hardness = def.hardness;
    bd.blastResistance = def.blast_resistance;
    registry.register_(std::move(bd));
  }
}

} // namespace voxel
