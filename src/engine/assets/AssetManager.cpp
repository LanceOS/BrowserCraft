#include "AssetManager.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <random>
#include <filesystem>
#include <vector>

#include "../../ext/json.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "../../ext/stb_image.h"

using json = nlohmann::json;

namespace voxel {

namespace {
auto findAssetRoot() -> std::string {
  namespace fs = std::filesystem;
  const std::string candidateFile = "assets/blocks.json";

  fs::path cursor = fs::current_path();
  for (int32_t depth = 0; depth < 12; ++depth) {
    fs::path blocksPath = cursor / candidateFile;
    if (fs::exists(blocksPath) && fs::is_regular_file(blocksPath)) {
      return (cursor / "assets").string();
    }

    if (!cursor.has_parent_path()) break;
    cursor = cursor.parent_path();
  }

  return "assets";
}

auto resolveTexturePath(const std::string& root, const std::string& textureName) -> std::string {
  return (std::filesystem::path(root) / "textures" / textureName).string();
}
} // namespace

AssetManager& AssetManager::get() {
    static AssetManager instance;
    return instance;
}

void AssetManager::loadAssets() {
    m_blockDefs.clear();
    m_textureData.clear();

    const std::string assetRoot = findAssetRoot();
    const std::string blocksFile = (std::filesystem::path(assetRoot) / "blocks.json").string();

    // Air is always ID 0
    m_airDef = {0, "Air", false, false, false, 0, 0.0f, 0.0f, -1, -1, -1};
    m_blockDefs[0] = m_airDef;

    std::ifstream file(blocksFile);
    if (!file.is_open()) {
        std::cerr << "Failed to open blocks.json at: " << blocksFile << std::endl;
        return;
    }

    json j;
    try {
        file >> j;
    } catch (const json::parse_error& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        return;
    }

    std::unordered_map<std::string, int> textureIndices;

    if (j.contains("textures")) {
        for (auto it = j["textures"].begin(); it != j["textures"].end(); ++it) {
            std::string texName = it.key();
            std::string filename = resolveTexturePath(assetRoot, it.value().get<std::string>());

            int width, height, channels;
            stbi_set_flip_vertically_on_load(true);
            unsigned char* data = stbi_load(filename.c_str(), &width, &height, &channels, 4);

            std::vector<uint8_t> imgData(16 * 16 * 4);
            if (data && width == 16 && height == 16) {
                std::copy(data, data + (16 * 16 * 4), imgData.begin());
            } else {
                if (data) std::cerr << "Texture " << filename << " is not 16x16. Procedurally generating fallback..." << std::endl;
                else std::cerr << "Failed to load texture " << filename << ". Procedurally generating fallback..." << std::endl;
                
                // Procedural generation fallback
                uint32_t color = 0xFFFFFFFF;
                bool hasTransparency = false;
                if (texName.find("grass") != std::string::npos) color = 0xFF228B22; // Green
                else if (texName.find("dirt") != std::string::npos) color = 0xFF13458B; // Brown (ABGR)
                else if (texName.find("stone") != std::string::npos) color = 0xFF808080; // Gray
                else if (texName.find("oak_log_top") != std::string::npos) color = 0xFF8B6914; // Brown-orange
                else if (texName.find("oak_log_side") != std::string::npos) color = 0xFF5C4033; // Dark brown
                else if (texName.find("oak_planks") != std::string::npos) color = 0xFFCD853F; // Wood planks
                else if (texName.find("oak_leaves") != std::string::npos) { color = 0xFF1E8B1E; hasTransparency = true; } // Green foliage
                else if (texName.find("water") != std::string::npos) { color = 0xFF1E3A8A; hasTransparency = true; } // Blue water
                else if (texName.find("lava") != std::string::npos) { color = 0xFFFF4500; } // Orange-red lava
                else if (texName.find("coal_ore") != std::string::npos) color = 0xFF333333; // Dark gray
                else if (texName.find("iron_ore") != std::string::npos) color = 0xFFD4A574; // Sandy brown
                else if (texName.find("gold_ore") != std::string::npos) color = 0xFFFFD700; // Gold
                else if (texName.find("diamond_ore") != std::string::npos) color = 0xFF4FC3F7; // Cyan-blue
                else if (texName.find("powerstone_ore") != std::string::npos) color = 0xFF9B30FF; // Purple

                std::mt19937 rng(std::hash<std::string>{}(texName));
                std::uniform_int_distribution<int> dist(-15, 15);

                for (int i = 0; i < 16 * 16; ++i) {
                    int r = std::clamp<int>((color & 0xFF) + dist(rng), 0, 255);
                    int g = std::clamp<int>(((color >> 8) & 0xFF) + dist(rng), 0, 255);
                    int b = std::clamp<int>(((color >> 16) & 0xFF) + dist(rng), 0, 255);
                    int a = hasTransparency ? 200 : 255;
                    imgData[i * 4 + 0] = r;
                    imgData[i * 4 + 1] = g;
                    imgData[i * 4 + 2] = b;
                    imgData[i * 4 + 3] = a;
                }
            }
            if (data) stbi_image_free(data);

            textureIndices[texName] = m_textureLayerCount;
            m_textureData.insert(m_textureData.end(), imgData.begin(), imgData.end());
            m_textureLayerCount++;
        }
    }

    if (j.contains("blocks")) {
        for (const auto& blockJson : j["blocks"]) {
            BlockDef def;
            def.id = blockJson["id"].get<uint8_t>();
            def.name = blockJson["name"].get<std::string>();
            def.is_opaque = blockJson.value("is_opaque", true);
            def.is_liquid = blockJson.value("is_liquid", false);
            def.is_foliage = blockJson.value("is_foliage", false);
            def.light_emission = blockJson.value("light_emission", 0);
            def.hardness = blockJson.value("hardness", 1.5f);
            def.blast_resistance = blockJson.value("blast_resistance", 6.0f);

            auto getTexIndex = [&](const std::string& name) -> int {
                auto it = textureIndices.find(name);
                return (it != textureIndices.end()) ? it->second : 0;
            };

            const auto& faces = blockJson["faces"];
            if (faces.contains("all")) {
                int tex = getTexIndex(faces["all"].get<std::string>());
                def.tex_top = tex;
                def.tex_bottom = tex;
                def.tex_side = tex;
            } else {
                def.tex_top = getTexIndex(faces.value("top", ""));
                def.tex_bottom = getTexIndex(faces.value("bottom", ""));
                def.tex_side = getTexIndex(faces.value("side", ""));
            }

            m_blockDefs[def.id] = def;
        }
    }
}

const BlockDef& AssetManager::getBlockDef(uint8_t id) const {
    auto it = m_blockDefs.find(id);
    if (it != m_blockDefs.end()) {
        return it->second;
    }
    return m_airDef;
}

size_t AssetManager::getBlockCount() const {
    return m_blockDefs.size();
}

const std::vector<uint8_t>& AssetManager::getTextureData() const {
    return m_textureData;
}

int AssetManager::getTextureLayerCount() const {
    return m_textureLayerCount;
}

} // namespace voxel
