#include "TerrainSaveData.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <ctime>

namespace terrain {

namespace {

#pragma pack(push, 1)
struct SerializedEdit {
  int32_t type;
  float centerX, centerY, centerZ;
  float radius;
  float strength;
  float normalX, normalY, normalZ;
  uint64_t timestamp;
};
#pragma pack(pop)

} // namespace

auto TerrainSaveData::load(const std::string& filePath, TerrainEditHistory& history) -> bool {
  history.clear();

  if (!std::filesystem::exists(filePath)) {
    return true; // File doesn't exist, which is normal for new worlds
  }

  std::ifstream file(filePath, std::ios::binary);
  if (!file) {
    return false;
  }

  uint32_t magic = 0;
  uint32_t version = 0;

  file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  file.read(reinterpret_cast<char*>(&version), sizeof(version));

  if (!file || magic != MAGIC) {
    std::cerr << "TerrainSaveData: invalid magic in " << filePath << "\n";
    return false;
  }

  if (version != CURRENT_VERSION) {
    std::cerr << "TerrainSaveData: unsupported version " << version << " in " << filePath << "\n";
    return false;
  }

  while (file) {
    SerializedEdit serialized{};
    file.read(reinterpret_cast<char*>(&serialized), sizeof(serialized));
    if (file.gcount() == sizeof(serialized)) {
      TerrainEdit edit{};
      edit.brush.type = static_cast<BrushType>(serialized.type);
      edit.brush.center = glm::vec3(serialized.centerX, serialized.centerY, serialized.centerZ);
      edit.brush.radius = serialized.radius;
      edit.brush.strength = serialized.strength;
      edit.brush.planeNormal = glm::vec3(serialized.normalX, serialized.normalY, serialized.normalZ);
      edit.timestamp = serialized.timestamp;
      history.addEdit(edit);
    }
  }

  return true;
}

auto TerrainSaveData::save(const std::string& filePath, const TerrainEditHistory& history) -> bool {
  // Write to a temporary file first and then rename it for atomic/crash-safe writes
  std::string tmpPath = filePath + ".tmp";
  {
    std::ofstream file(tmpPath, std::ios::binary);
    if (!file) {
      return false;
    }

    uint32_t magic = MAGIC;
    uint32_t version = CURRENT_VERSION;

    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));

    auto edits = history.getEdits();
    for (const auto& edit : edits) {
      SerializedEdit serialized{
        static_cast<int32_t>(edit.brush.type),
        edit.brush.center.x, edit.brush.center.y, edit.brush.center.z,
        edit.brush.radius,
        edit.brush.strength,
        edit.brush.planeNormal.x, edit.brush.planeNormal.y, edit.brush.planeNormal.z,
        edit.timestamp
      };
      file.write(reinterpret_cast<const char*>(&serialized), sizeof(serialized));
    }

    if (!file.good()) {
      file.close();
      std::error_code ec;
      std::filesystem::remove(tmpPath, ec);
      return false;
    }
  }

  std::error_code ec;
  std::filesystem::rename(tmpPath, filePath, ec);
  return !ec;
}

auto TerrainSaveData::append(const std::string& filePath, const TerrainEdit& edit) -> bool {
  bool needsHeader = !std::filesystem::exists(filePath) || std::filesystem::file_size(filePath) == 0;

  std::ofstream file(filePath, std::ios::binary | std::ios::app);
  if (!file) {
    return false;
  }

  if (needsHeader) {
    uint32_t magic = MAGIC;
    uint32_t version = CURRENT_VERSION;
    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
  }

  SerializedEdit serialized{
    static_cast<int32_t>(edit.brush.type),
    edit.brush.center.x, edit.brush.center.y, edit.brush.center.z,
    edit.brush.radius,
    edit.brush.strength,
    edit.brush.planeNormal.x, edit.brush.planeNormal.y, edit.brush.planeNormal.z,
    edit.timestamp
  };
  file.write(reinterpret_cast<const char*>(&serialized), sizeof(serialized));

  return file.good();
}

} // namespace terrain
