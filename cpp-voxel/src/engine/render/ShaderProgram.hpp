#pragma once

#include "gl_core.hpp"
#include <string>
#include <unordered_map>
#include <optional>
#include <cstdint>

namespace voxel {

/// Compiled and linked OpenGL shader program with uniform caching.
class ShaderProgram {
public:
  ShaderProgram(const std::string& vertexSource, const std::string& fragmentSource);
  ~ShaderProgram();

  ShaderProgram(const ShaderProgram&) = delete;
  ShaderProgram& operator=(const ShaderProgram&) = delete;
  ShaderProgram(ShaderProgram&& other) noexcept;
  ShaderProgram& operator=(ShaderProgram&& other) noexcept;

  void use() const;
  void bindUniformBlock(const std::string& blockName, uint32_t binding);
  [[nodiscard]] auto uniform(const std::string& name) -> int32_t;
  [[nodiscard]] auto program() const -> uint32_t { return m_program; }

private:
  auto compile(uint32_t type, const std::string& source) -> uint32_t;

  uint32_t m_program = 0;
  std::unordered_map<std::string, int32_t> m_uniformCache;
};

} // namespace voxel
