#include "GrassFactory.hpp"
#include "world/BlockDefinition.hpp"
#include "world/BlockIds.hpp"

namespace voxel::flora {

GrassFactory::GrassFactory(std::vector<GrassSpeciesDefinition> grassList)
  : m_grassList(std::move(grassList))
{
  for (size_t i = 0; i < m_grassList.size(); ++i) {
    m_speciesIndex[m_grassList[i].speciesId] = i;
  }
}

void GrassFactory::registerBlocks(BlockRegistry& registry) {
  for (const auto& def : m_grassList) {
    // Skip if already registered (e.g., from blocks.json)
    if (registry.tryGet(def.blockId)) continue;

    BlockDefinition bd;
    bd.id = def.blockId;
    bd.name = def.name;
    bd.textures.top = def.textureLayer;
    bd.textures.bottom = def.textureLayer;
    bd.textures.side = def.textureLayer;
    bd.material.opaque = false;
    bd.material.transparent = true;
    bd.material.foliage = true;
    bd.material.liquid = false;
    bd.material.lightEmission = 0;
    bd.collision = EMPTY_BLOCK_AABB;  // No collision — pass-through
    bd.hardness = 0.0f;
    bd.blastResistance = 0.0f;
    registry.register_(std::move(bd));
  }
}

FloraProperties GrassFactory::getProperties(uint16_t speciesId) const {
  auto it = m_speciesIndex.find(speciesId);
  if (it == m_speciesIndex.end()) {
    throw std::runtime_error("Unknown grass species " + std::to_string(speciesId));
  }
  const auto& def = m_grassList[it->second];
  return {
    .blockId = def.blockId,
    .name = def.name,
    .renderType = def.isTall ? FloraRenderType::TALL_CROSS_QUAD : FloraRenderType::CROSS_QUAD,
    .textureLayers = {static_cast<uint16_t>(def.textureLayer)},
    .acceptableSoil = def.acceptableSoil,
    .biomeAffinity = def.biomeAffinity,
    .lightRequirements = {8, 0, 15},
    .collision = EMPTY_BLOCK_AABB,
    .dropsSelf = true,
    .dropItemId = def.blockId,
    .dropCountMin = 1,
    .dropCountMax = 1,
    .boneMealable = false,
  };
}

std::vector<FloraProperties> GrassFactory::getAllProperties() const {
  std::vector<FloraProperties> props;
  props.reserve(m_grassList.size());
  for (const auto& def : m_grassList) {
    props.push_back(getProperties(def.speciesId));
  }
  return props;
}

const GrassSpeciesDefinition* GrassFactory::getSpecies(uint16_t speciesId) const {
  auto it = m_speciesIndex.find(speciesId);
  if (it == m_speciesIndex.end()) return nullptr;
  return &m_grassList[it->second];
}

} // namespace voxel::flora
