#include "gl_core.hpp"

namespace voxel::gl {

#define LOAD(name) do { \
    name = reinterpret_cast<decltype(name)>(glfwGetProcAddress("gl" #name)); \
    if (!name) { \
      throw std::runtime_error("Failed to load GL function: gl" #name); \
    } \
  } while(0)

PFN_CreateShader CreateShader;
PFN_ShaderSource ShaderSource;
PFN_CompileShader CompileShader;
PFN_GetShaderiv GetShaderiv;
PFN_GetShaderInfoLog GetShaderInfoLog;
PFN_DeleteShader DeleteShader;
PFN_CreateProgram CreateProgram;
PFN_AttachShader AttachShader;
PFN_LinkProgram LinkProgram;
PFN_GetProgramiv GetProgramiv;
PFN_GetProgramInfoLog GetProgramInfoLog;
PFN_UseProgram UseProgram;
PFN_DeleteProgram DeleteProgram;
PFN_GetUniformLocation GetUniformLocation;
PFN_Uniform1i Uniform1i;
PFN_Uniform1f Uniform1f;
PFN_Uniform3f Uniform3f;
PFN_UniformMatrix4fv UniformMatrix4fv;
PFN_GetUniformBlockIndex GetUniformBlockIndex;
PFN_UniformBlockBinding UniformBlockBinding;

PFN_GenVertexArrays GenVertexArrays;
PFN_BindVertexArray BindVertexArray;
PFN_DeleteVertexArrays DeleteVertexArrays;
PFN_EnableVertexAttribArray EnableVertexAttribArray;
PFN_VertexAttribPointer VertexAttribPointer;

PFN_GenBuffers GenBuffers;
PFN_BindBuffer BindBuffer;
PFN_BufferData BufferData;
PFN_BufferSubData BufferSubData;
PFN_DeleteBuffers DeleteBuffers;
PFN_BindBufferBase BindBufferBase;

PFN_GenTextures GenTextures;
PFN_BindTexture BindTexture;
PFN_TexStorage3D TexStorage3D;
PFN_TexSubImage3D TexSubImage3D;
PFN_GenerateMipmap GenerateMipmap;
PFN_TexParameteri TexParameteri;
PFN_ActiveTexture ActiveTexture;
PFN_DeleteTextures DeleteTextures;

PFN_DrawElements DrawElements;
PFN_DrawArrays DrawArrays;

PFN_Enable Enable;
PFN_Disable Disable;
PFN_BlendFunc BlendFunc;
PFN_DepthMask DepthMask;
PFN_DepthFunc DepthFunc;
PFN_Clear Clear;
PFN_ClearColor ClearColor;
PFN_Viewport Viewport;

void loadGLFunctions() {
  LOAD(CreateShader);
  LOAD(ShaderSource);
  LOAD(CompileShader);
  LOAD(GetShaderiv);
  LOAD(GetShaderInfoLog);
  LOAD(DeleteShader);
  LOAD(CreateProgram);
  LOAD(AttachShader);
  LOAD(LinkProgram);
  LOAD(GetProgramiv);
  LOAD(GetProgramInfoLog);
  LOAD(UseProgram);
  LOAD(DeleteProgram);
  LOAD(GetUniformLocation);
  LOAD(Uniform1i);
  LOAD(Uniform1f);
  LOAD(Uniform3f);
  LOAD(UniformMatrix4fv);
  LOAD(GetUniformBlockIndex);
  LOAD(UniformBlockBinding);

  LOAD(GenVertexArrays);
  LOAD(BindVertexArray);
  LOAD(DeleteVertexArrays);
  LOAD(EnableVertexAttribArray);
  LOAD(VertexAttribPointer);

  LOAD(GenBuffers);
  LOAD(BindBuffer);
  LOAD(BufferData);
  LOAD(BufferSubData);
  LOAD(DeleteBuffers);
  LOAD(BindBufferBase);

  LOAD(GenTextures);
  LOAD(BindTexture);
  LOAD(TexStorage3D);
  LOAD(TexSubImage3D);
  LOAD(GenerateMipmap);
  LOAD(TexParameteri);
  LOAD(ActiveTexture);
  LOAD(DeleteTextures);

  LOAD(DrawElements);
  LOAD(DrawArrays);

  LOAD(Enable);
  LOAD(Disable);
  LOAD(BlendFunc);
  LOAD(DepthMask);
  LOAD(DepthFunc);
  LOAD(Clear);
  LOAD(ClearColor);
  LOAD(Viewport);
}

#undef LOAD

} // namespace voxel::gl
