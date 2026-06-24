#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <memory>

namespace voxel {

struct BlockDef {
    uint8_t id;
    std::string name;
    bool is_opaque;
    int tex_top;
    int tex_bottom;
    int tex_side;
};

class AssetManager {
public:
    static AssetManager& get();

    void loadAssets();

    const BlockDef& getBlockDef(uint8_t id) const;
    
    // Total number of blocks defined (including air)
    size_t getBlockCount() const;

    const std::unordered_map<uint8_t, BlockDef>& getBlockDefs() const { return m_blockDefs; }

    // Get the raw texture data for the GL_TEXTURE_2D_ARRAY
    // The textures are always loaded as 16x16 RGBA
    const std::vector<uint8_t>& getTextureData() const;
    int getTextureSize() const { return 16; } // Hardcoded 16x16 for now
    int getTextureLayerCount() const;

private:
    AssetManager() = default;
    ~AssetManager() = default;

    std::unordered_map<uint8_t, BlockDef> m_blockDefs;
    BlockDef m_airDef;

    std::vector<uint8_t> m_textureData;
    int m_textureLayerCount = 0;
};

} // namespace voxel
