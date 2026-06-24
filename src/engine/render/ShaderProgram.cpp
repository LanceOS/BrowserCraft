#include "ShaderProgram.hpp"
#include <stdexcept>

namespace voxel {

ShaderProgram::ShaderProgram(const std::string& vertexSource, const std::string& fragmentSource) {
  uint32_t vs = compile(GL_VERTEX_SHADER, vertexSource);
  uint32_t fs = compile(GL_FRAGMENT_SHADER, fragmentSource);

  m_program = gl::CreateProgram();
  gl::AttachShader(m_program, vs);
  gl::AttachShader(m_program, fs);
  gl::LinkProgram(m_program);

  GLint linked = 0;
  gl::GetProgramiv(m_program, GL_LINK_STATUS, &linked);
  if (!linked) {
    char log[1024];
    gl::GetProgramInfoLog(m_program, sizeof(log), nullptr, log);
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);
    gl::DeleteProgram(m_program);
    throw std::runtime_error(std::string("Shader link failed: ") + log);
  }

  gl::DeleteShader(vs);
  gl::DeleteShader(fs);
}

ShaderProgram::~ShaderProgram() {
  if (m_program) gl::DeleteProgram(m_program);
}

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept : m_program(other.m_program) {
  other.m_program = 0;
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
  if (this != &other) {
    if (m_program) gl::DeleteProgram(m_program);
    m_program = other.m_program;
    other.m_program = 0;
  }
  return *this;
}

void ShaderProgram::use() const {
  gl::UseProgram(m_program);
}

void ShaderProgram::bindUniformBlock(const std::string& blockName, uint32_t binding) {
  uint32_t idx = gl::GetUniformBlockIndex(m_program, blockName.c_str());
  if (idx != GL_INVALID_INDEX) {
    gl::UniformBlockBinding(m_program, idx, binding);
  }
}

auto ShaderProgram::uniform(const std::string& name) -> int32_t {
  auto it = m_uniformCache.find(name);
  if (it != m_uniformCache.end()) return it->second;
  int32_t loc = gl::GetUniformLocation(m_program, name.c_str());
  m_uniformCache[name] = loc;
  return loc;
}

auto ShaderProgram::compile(uint32_t type, const std::string& source) -> uint32_t {
  uint32_t shader = gl::CreateShader(type);
  const char* src = source.c_str();
  gl::ShaderSource(shader, 1, &src, nullptr);
  gl::CompileShader(shader);

  GLint compiled = 0;
  gl::GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    char log[1024];
    gl::GetShaderInfoLog(shader, sizeof(log), nullptr, log);
    gl::DeleteShader(shader);
    throw std::runtime_error(std::string("Shader compile failed: ") + log);
  }
  return shader;
}

} // namespace voxel
