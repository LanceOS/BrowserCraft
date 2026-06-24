#pragma once

// Minimal OpenGL 4.6 Core loader for the voxel engine.
// Loads the subset of GL functions we use via GLFW.

#include <GLFW/glfw3.h>
#include <stdexcept>
#include <string>
#include <cstdint>

namespace voxel::gl {

// ---- Explicit function pointer types ----
using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLint = int;
using GLsizei = int;
using GLsizeiptr = std::ptrdiff_t;
using GLuint = unsigned int;
using GLchar = char;
using GLintptr = std::ptrdiff_t;

// Shader
using PFN_CreateShader = GLuint (*)(GLenum);
using PFN_ShaderSource = void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using PFN_CompileShader = void (*)(GLuint);
using PFN_GetShaderiv = void (*)(GLuint, GLenum, GLint*);
using PFN_GetShaderInfoLog = void (*)(GLuint, GLsizei, GLsizei*, GLchar*);
using PFN_DeleteShader = void (*)(GLuint);
using PFN_CreateProgram = GLuint (*)();
using PFN_AttachShader = void (*)(GLuint, GLuint);
using PFN_LinkProgram = void (*)(GLuint);
using PFN_GetProgramiv = void (*)(GLuint, GLenum, GLint*);
using PFN_GetProgramInfoLog = void (*)(GLuint, GLsizei, GLsizei*, GLchar*);
using PFN_UseProgram = void (*)(GLuint);
using PFN_DeleteProgram = void (*)(GLuint);
using PFN_GetUniformLocation = GLint (*)(GLuint, const GLchar*);
using PFN_Uniform1i = void (*)(GLint, GLint);
using PFN_Uniform1f = void (*)(GLint, GLfloat);
using PFN_Uniform3f = void (*)(GLint, GLfloat, GLfloat, GLfloat);
using PFN_UniformMatrix4fv = void (*)(GLint, GLsizei, GLboolean, const GLfloat*);
using PFN_GetUniformBlockIndex = GLuint (*)(GLuint, const GLchar*);
using PFN_UniformBlockBinding = void (*)(GLuint, GLuint, GLuint);

// VAO
using PFN_GenVertexArrays = void (*)(GLsizei, GLuint*);
using PFN_BindVertexArray = void (*)(GLuint);
using PFN_DeleteVertexArrays = void (*)(GLsizei, const GLuint*);
using PFN_EnableVertexAttribArray = void (*)(GLuint);
using PFN_VertexAttribPointer = void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);

// Buffer
using PFN_GenBuffers = void (*)(GLsizei, GLuint*);
using PFN_BindBuffer = void (*)(GLenum, GLuint);
using PFN_BufferData = void (*)(GLenum, GLsizeiptr, const void*, GLenum);
using PFN_BufferSubData = void (*)(GLenum, GLintptr, GLsizeiptr, const void*);
using PFN_DeleteBuffers = void (*)(GLsizei, const GLuint*);
using PFN_BindBufferBase = void (*)(GLenum, GLuint, GLuint);
using PFN_CreateBuffers = void (*)(GLsizei, GLuint*);
using PFN_NamedBufferStorage = void (*)(GLuint, GLsizeiptr, const void*, GLbitfield);
using PFN_MapNamedBufferRange = void* (*)(GLuint, GLintptr, GLsizeiptr, GLbitfield);
using PFN_UnmapNamedBuffer = GLboolean (*)(GLuint);

// Texture
using PFN_GenTextures = void (*)(GLsizei, GLuint*);
using PFN_BindTexture = void (*)(GLenum, GLuint);
using PFN_TexStorage3D = void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei);
using PFN_TexSubImage3D = void (*)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*);
using PFN_GenerateMipmap = void (*)(GLenum);
using PFN_TexParameteri = void (*)(GLenum, GLenum, GLint);
using PFN_ActiveTexture = void (*)(GLenum);
using PFN_DeleteTextures = void (*)(GLsizei, const GLuint*);

// Draw
using PFN_DrawElements = void (*)(GLenum, GLsizei, GLenum, const void*);
using PFN_DrawArrays = void (*)(GLenum, GLint, GLsizei);
using PFN_DrawElementsBaseVertex = void (*)(GLenum, GLsizei, GLenum, const void*, GLint);
using PFN_MultiDrawElementsIndirect = void (*)(GLenum, GLenum, const void*, GLsizei, GLsizei);

// Compute
using PFN_DispatchCompute = void (*)(GLuint, GLuint, GLuint);
using PFN_MemoryBarrier = void (*)(GLbitfield);

// State
using PFN_Enable = void (*)(GLenum);
using PFN_Disable = void (*)(GLenum);
using PFN_BlendFunc = void (*)(GLenum, GLenum);
using PFN_DepthMask = void (*)(GLboolean);
using PFN_DepthFunc = void (*)(GLenum);
using PFN_Clear = void (*)(GLbitfield);
using PFN_ClearColor = void (*)(GLfloat, GLfloat, GLfloat, GLfloat);
using PFN_Viewport = void (*)(GLint, GLint, GLsizei, GLsizei);

// ---- Extern declarations ----
extern PFN_CreateShader CreateShader;
extern PFN_ShaderSource ShaderSource;
extern PFN_CompileShader CompileShader;
extern PFN_GetShaderiv GetShaderiv;
extern PFN_GetShaderInfoLog GetShaderInfoLog;
extern PFN_DeleteShader DeleteShader;
extern PFN_CreateProgram CreateProgram;
extern PFN_AttachShader AttachShader;
extern PFN_LinkProgram LinkProgram;
extern PFN_GetProgramiv GetProgramiv;
extern PFN_GetProgramInfoLog GetProgramInfoLog;
extern PFN_UseProgram UseProgram;
extern PFN_DeleteProgram DeleteProgram;
extern PFN_GetUniformLocation GetUniformLocation;
extern PFN_Uniform1i Uniform1i;
extern PFN_Uniform1f Uniform1f;
extern PFN_Uniform3f Uniform3f;
extern PFN_UniformMatrix4fv UniformMatrix4fv;
extern PFN_GetUniformBlockIndex GetUniformBlockIndex;
extern PFN_UniformBlockBinding UniformBlockBinding;

extern PFN_GenVertexArrays GenVertexArrays;
extern PFN_BindVertexArray BindVertexArray;
extern PFN_DeleteVertexArrays DeleteVertexArrays;
extern PFN_EnableVertexAttribArray EnableVertexAttribArray;
extern PFN_VertexAttribPointer VertexAttribPointer;

extern PFN_GenBuffers GenBuffers;
extern PFN_BindBuffer BindBuffer;
extern PFN_BufferData BufferData;
extern PFN_BufferSubData BufferSubData;
extern PFN_DeleteBuffers DeleteBuffers;
extern PFN_BindBufferBase BindBufferBase;
extern PFN_CreateBuffers CreateBuffers;
extern PFN_NamedBufferStorage NamedBufferStorage;
extern PFN_MapNamedBufferRange MapNamedBufferRange;
extern PFN_UnmapNamedBuffer UnmapNamedBuffer;

extern PFN_GenTextures GenTextures;
extern PFN_BindTexture BindTexture;
extern PFN_TexStorage3D TexStorage3D;
extern PFN_TexSubImage3D TexSubImage3D;
extern PFN_GenerateMipmap GenerateMipmap;
extern PFN_TexParameteri TexParameteri;
extern PFN_ActiveTexture ActiveTexture;
extern PFN_DeleteTextures DeleteTextures;

extern PFN_DrawElements DrawElements;
extern PFN_DrawArrays DrawArrays;
extern PFN_DrawElementsBaseVertex DrawElementsBaseVertex;
extern PFN_MultiDrawElementsIndirect MultiDrawElementsIndirect;

extern PFN_DispatchCompute DispatchCompute;
extern PFN_MemoryBarrier MemoryBarrier;

extern PFN_Enable Enable;
extern PFN_Disable Disable;
extern PFN_BlendFunc BlendFunc;
extern PFN_DepthMask DepthMask;
extern PFN_DepthFunc DepthFunc;
extern PFN_Clear Clear;
extern PFN_ClearColor ClearColor;
extern PFN_Viewport Viewport;

void loadGLFunctions();

// GL constants for Compute and Indirect
#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#endif
#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#endif
#ifndef GL_DRAW_INDIRECT_BUFFER
#define GL_DRAW_INDIRECT_BUFFER 0x8F3F
#endif
#ifndef GL_COMMAND_BARRIER_BIT
#define GL_COMMAND_BARRIER_BIT 0x00000040
#endif
#ifndef GL_SHADER_STORAGE_BARRIER_BIT
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#endif

} // namespace voxel::gl
