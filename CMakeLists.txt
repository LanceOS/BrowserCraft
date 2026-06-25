cmake_minimum_required(VERSION 3.21)
project(voxel-engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Dependencies via FetchContent (no system packages needed) ----
include(FetchContent)

# GLM (header-only math library)
FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
)
set(GLM_TEST_ENABLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

# GLFW (windowing + input)
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# ImGui (docking branch for better UI)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.6-docking
)
FetchContent_MakeAvailable(imgui)

# Catch2 (test framework)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1
)
FetchContent_MakeAvailable(Catch2)
include(${catch2_SOURCE_DIR}/extras/Catch.cmake)

# System libraries
find_package(OpenGL REQUIRED)

# ---- Engine library ----
add_library(voxel_engine STATIC
  src/engine/math/AABB.cpp
  src/engine/math/Frustum.cpp
  src/engine/alloc/RingBuffer.cpp
  src/engine/alloc/ScratchArena.cpp
  src/engine/alloc/TypedArrayPool.cpp
  src/engine/alloc/LockFreeRingBuffer.cpp
  src/engine/alloc/SharedPool.cpp
  src/engine/ecs/EntityManager.cpp
  src/engine/ecs/SystemManager.cpp
  src/engine/assets/AssetManager.cpp
  src/world/BlockRegistry.cpp
  src/world/Chunk.cpp
  src/world/ChunkManager.cpp
  src/world/World.cpp
  src/world/daynight/DayNightCycle.cpp
  src/engine/render/gl_core.cpp
  src/engine/render/ShaderProgram.cpp
  src/engine/render/VertexBuffer.cpp
  src/engine/render/VertexArray.cpp
  src/engine/render/UniformBuffer.cpp
  src/engine/render/Texture2DArray.cpp
  src/engine/render/ChunkMesh.cpp
  src/engine/render/Renderer.cpp
  src/engine/core/InputState.cpp
  src/engine/core/GameLoop.cpp
  src/engine/audio/AudioEngine.cpp
  src/engine/threading/WorkerThreadPool.cpp
  src/engine/save/SaveManager.cpp
  src/world/generation/SimplexNoise.cpp
  src/world/generation/CaveCarver.cpp
  src/world/generation/OreDistributor.cpp
  src/world/generation/WorldGenPipeline.cpp
  src/engine/workers/mesher/GreedyMesher.cpp
  src/content/biomes/BiomeSampler.cpp
  src/game/GameSession.cpp
  src/game/Game.cpp
  src/ui/UIManager.cpp
  # ImGui core + GLFW/OpenGL3 backends
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(voxel_engine PUBLIC src ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)

target_link_libraries(voxel_engine PUBLIC
  glm::glm
  glfw
  OpenGL::GL
)

# ---- Main executable ----
add_executable(voxel_app src/main.cpp)
target_link_libraries(voxel_app PRIVATE voxel_engine glfw OpenGL::GL)

# ---- Tests ----
enable_testing()
add_executable(voxel_tests
  tests/main.cpp
  tests/test_aabb.cpp
  tests/test_frustum.cpp
  tests/test_ring_buffer.cpp
  tests/test_scratch_arena.cpp
  tests/test_lockfree_queue.cpp
  tests/test_shared_pool.cpp
  tests/test_entity_manager.cpp
  tests/test_component_store.cpp
  tests/test_query.cpp
  tests/test_system_manager.cpp
  tests/test_block_registry.cpp
  tests/test_world.cpp
  tests/test_simplex_noise.cpp
  tests/test_biome_sampler.cpp
  tests/test_worldgen.cpp
  tests/test_save_manager.cpp
  tests/test_thread_pool.cpp
)
target_link_libraries(voxel_tests PRIVATE voxel_engine Catch2::Catch2WithMain)
catch_discover_tests(voxel_tests)
