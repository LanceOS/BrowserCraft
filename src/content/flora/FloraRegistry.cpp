#include "FloraRegistry.hpp"

namespace voxel::flora {

void FloraRegistry::registerCategory(std::unique_ptr<FloraCategoryFactory> factory) {
  if (!factory) return;

  std::string name(factory->categoryName());
  m_byName[name] = factory.get();

  // Collect all properties for this category
  for (const auto& props : factory->getAllProperties()) {
    m_byBlockId[props.blockId] = props;
  }

  m_categories.push_back(std::move(factory));
}

FloraCategoryFactory* FloraRegistry::getCategory(std::string_view name) const {
  auto it = m_byName.find(std::string(name));
  return (it != m_byName.end()) ? it->second : nullptr;
}

const FloraProperties* FloraRegistry::getProperties(uint16_t blockId) const {
  auto it = m_byBlockId.find(blockId);
  return (it != m_byBlockId.end()) ? &it->second : nullptr;
}

bool FloraRegistry::isFlora(uint16_t blockId) const {
  return m_byBlockId.find(blockId) != m_byBlockId.end();
}

void FloraRegistry::registerAllBlocks(BlockRegistry& registry) {
  for (const auto& factory : m_categories) {
    factory->registerBlocks(registry);
  }
}

} // namespace voxel::flora
