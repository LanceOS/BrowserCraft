#pragma once

#include "gl_core.hpp"
#include "PersistentBuffer.hpp"
#include "ShaderProgram.hpp"
#include "shaders/ShaderSources.hpp"
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>

namespace voxel {

struct ChunkCullData {
    float min[4]; // vec4
    float max[4]; // vec4
    uint32_t indexCount;
    uint32_t firstIndex;
    uint32_t baseVertex;
    uint32_t slotIndex;
    uint32_t pad1;
    uint32_t pad2;
    uint32_t pad3;
    uint32_t pad4;
};

struct DrawCommand {
    uint32_t count;
    uint32_t instanceCount;
    uint32_t firstIndex;
    uint32_t baseVertex;
    uint32_t baseInstance;
};

class IndirectBatcher {
public:
  IndirectBatcher(uint32_t maxChunks) : m_maxChunks(maxChunks) {
      // Compile compute shader
      uint32_t cs = gl::CreateShader(GL_COMPUTE_SHADER);
      gl::ShaderSource(cs, 1, &shaders::cullingCompute, nullptr);
      gl::CompileShader(cs);
      int success;
      gl::GetShaderiv(cs, GL_COMPILE_STATUS, &success);
      if (!success) {
          char info[512];
          gl::GetShaderInfoLog(cs, 512, nullptr, info);
          throw std::runtime_error("Compute shader compilation failed: " + std::string(info));
      }
      
      m_computeProgram = gl::CreateProgram();
      gl::AttachShader(m_computeProgram, cs);
      gl::LinkProgram(m_computeProgram);
      gl::DeleteShader(cs);

      // Create SSBOs
      m_chunkDataBuffer = std::make_unique<PersistentBuffer>(maxChunks * sizeof(ChunkCullData), GL_SHADER_STORAGE_BUFFER);
      m_indirectBuffer = std::make_unique<PersistentBuffer>(maxChunks * sizeof(DrawCommand), GL_DRAW_INDIRECT_BUFFER);
      
      // Zero out
      std::memset(m_chunkDataBuffer->mappedPtr(), 0, m_chunkDataBuffer->capacity());
      std::memset(m_indirectBuffer->mappedPtr(), 0, m_indirectBuffer->capacity());
  }

  ~IndirectBatcher() {
      if (m_computeProgram) {
          gl::DeleteProgram(m_computeProgram);
      }
  }

  uint32_t capacity() const { return m_maxChunks; }

  void updateChunkData(uint32_t slotIndex, const ChunkCullData& data) {
      if (slotIndex >= m_maxChunks) return;
      auto* ptr = static_cast<ChunkCullData*>(m_chunkDataBuffer->mappedPtr());
      ptr[slotIndex] = data;
  }

  void dispatchCulling(const float* projViewMatrix) {
      gl::UseProgram(m_computeProgram);

      int projViewLoc = gl::GetUniformLocation(m_computeProgram, "u_projView");
      gl::UniformMatrix4fv(projViewLoc, 1, GL_FALSE, projViewMatrix);

      int maxChunksLoc = gl::GetUniformLocation(m_computeProgram, "u_maxChunks");
      gl::Uniform1i(maxChunksLoc, m_maxChunks);

      gl::BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_chunkDataBuffer->buffer());
      gl::BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_indirectBuffer->buffer());

      // Dispatch 64 threads per workgroup
      uint32_t groups = (m_maxChunks + 63) / 64;
      gl::DispatchCompute(groups, 1, 1);

      // Ensure compute writes are visible to indirect drawing and SSBO reads in vertex shader
      gl::MemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
  }

  void drawIndirect() const {
      gl::BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_chunkDataBuffer->buffer());
      gl::BindBuffer(GL_DRAW_INDIRECT_BUFFER, m_indirectBuffer->buffer());
      gl::MultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr, m_maxChunks, sizeof(DrawCommand));
      gl::BindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
  }

private:
  uint32_t m_maxChunks = 0;
  uint32_t m_computeProgram = 0;
  std::unique_ptr<PersistentBuffer> m_chunkDataBuffer;
  std::unique_ptr<PersistentBuffer> m_indirectBuffer;
};

} // namespace voxel
