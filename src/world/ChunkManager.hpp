#pragma once

#include "Chunk.hpp"
#include <unordered_map>
#include <optional>
#include <cstdint>

// @deprecated Legacy voxel-world code retained during the render-only migration to triangle meshes.
namespace voxel {

/// Manages the set of active chunks around the player.
class ChunkManager {
public:
  /// Get a chunk by coordinates, or nullptr if not loaded.
  [[nodiscard]] auto get(int32_t chunkX, int32_t chunkZ) const -> const Chunk* {
    auto it = m_chunks.find(Chunk::makeKey(chunkX, chunkZ));
    return it != m_chunks.end() ? &it->second : nullptr;
  }

  /// Get a mutable chunk by coordinates.
  [[nodiscard]] auto getMut(int32_t chunkX, int32_t chunkZ) -> Chunk* {
    auto it = m_chunks.find(Chunk::makeKey(chunkX, chunkZ));
    return it != m_chunks.end() ? &it->second : nullptr;
  }

  /// Insert a chunk.
  void set(Chunk chunk) {
    auto key = chunk.key();
    m_chunks[key] = std::move(chunk);
  }

  /// Check if a chunk is loaded.
  [[nodiscard]] auto has(int32_t chunkX, int32_t chunkZ) const -> bool {
    return m_chunks.contains(Chunk::makeKey(chunkX, chunkZ));
  }

  /// Check by packed key.
  [[nodiscard]] auto hasKey(int64_t key) const -> bool {
    return m_chunks.contains(key);
  }

  /// Remove a chunk.
  auto remove(int32_t chunkX, int32_t chunkZ) -> bool {
    return m_chunks.erase(Chunk::makeKey(chunkX, chunkZ)) > 0;
  }

  /// Iterate over all chunks.
  template <typename F>
  void forEach(F&& callback) const {
    for (const auto& [key, chunk] : m_chunks) {
      callback(chunk);
    }
  }

  template <typename F>
  void forEach(F&& callback) {
    for (auto& [key, chunk] : m_chunks) {
      callback(chunk);
    }
  }

  [[nodiscard]] auto size() const -> size_t { return m_chunks.size(); }

  /// Expose entries for iteration (key, chunk pairs).
  template <typename F>
  void forEachEntry(F&& callback) const {
    for (const auto& [key, chunk] : m_chunks) {
      callback(key, chunk);
    }
  }

  template <typename F>
  void forEachEntry(F&& callback) {
    for (auto& [key, chunk] : m_chunks) {
      callback(key, chunk);
    }
  }

private:
  std::unordered_map<int64_t, Chunk> m_chunks;
};

} // namespace voxel
