cmake_minimum_required(VERSION 3.21)
project(voxel-engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Compiler cache (ccache) ----
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  message(STATUS "ccache found: ${CCACHE_PROGRAM}")
endif()

# ---- Fast linker (mold) ----
find_program(MOLD_LINKER mold)
if(MOLD_LINKER)
  add_link_options("-fuse-ld=mold")
  message(STATUS "Using mold linker")
endif()

# ---- Unity / jumbo builds (off by default, enable with -DUSE_UNITY_BUILD=ON) ----
option(USE_UNITY_BUILD "Enable unity/jumbo builds for faster full rebuilds" OFF)
if(USE_UNITY_BUILD)
  set(CMAKE_UNITY_BUILD ON)
  set(CMAKE_UNITY_BUILD_BATCH_SIZE 8)
  message(STATUS "Unity/jumbo builds enabled")
endif()

# ---- Dependencies ----
include(FetchContent)

# GLM (header-only math library) — try system package first, fall back to FetchContent
find_package(glm QUIET)
if(NOT glm_FOUND)
  FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
  )
  set(GLM_TEST_ENABLE OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(glm)
endif()

# GLFW (windowing + input) — try system package first, fall back to FetchContent
find_package(glfw3 QUIET)
if(glfw3_FOUND)
  set(GLFW_TARGET glfw::glfw)
else()
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
  )
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(glfw)
  set(GLFW_TARGET glfw)
endif()

# ImGui (docking branch for better UI)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.6-docking
)
FetchContent_MakeAvailable(imgui)

# Catch2 (test framework) — try system package first, fall back to FetchContent
find_package(Catch2 QUIET)
if(NOT Catch2_FOUND)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
  )
  FetchContent_MakeAvailable(Catch2)
  include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
else()
  include(Catch)
endif()

# System libraries
find_package(OpenGL REQUIRED)

# SQLite3 — try pkg-config first (system package), fall back to FindSQLite3
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(SQLITE3 sqlite3)
endif()
if(NOT SQLITE3_FOUND)
  find_path(SQLITE3_INCLUDE_DIRS sqlite3.h)
  find_library(SQLITE3_LIBRARIES sqlite3)
  if(SQLITE3_INCLUDE_DIRS AND SQLITE3_LIBRARIES)
    set(SQLITE3_FOUND TRUE)
  endif()
endif()
if(NOT SQLITE3_FOUND)
  message(FATAL_ERROR "SQLite3 development library not found. Install libsqlite3-dev.")
endif()
message(STATUS "SQLite3 found: ${SQLITE3_INCLUDE_DIRS}")

# ---- ImGui library (stable third-party, separate target to avoid recompilation) ----
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
# Exclude demo from release builds
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/imgui_demo.cpp)
endif()
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC ${GLFW_TARGET} OpenGL::GL)

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
  src/engine/collision/EntityCollisions.cpp
  src/engine/ecs/systems/PlayerControllerSystem.cpp
  src/engine/ecs/systems/PlayerSpawnSystem.cpp
  src/engine/assets/AssetManager.cpp
  src/world/BlockRegistry.cpp
  src/world/blocks/VanillaBlockFactory.cpp
  src/world/VoxelStore.cpp
  src/world/ChunkJobQueue.cpp
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
  src/engine/render/ChunkMeshAllocator.cpp
  src/engine/render/ChunkSyncer.cpp
  src/engine/render/DrawDispatcher.cpp
  src/engine/render/Renderer.cpp
  src/engine/core/InputState.cpp
  src/engine/core/GameLoop.cpp
  src/engine/audio/AudioEngine.cpp
  src/engine/threading/WorkerThreadPool.cpp
  src/engine/save/SaveManager.cpp
  src/engine/save/WorldNamingService.cpp
  src/engine/save/WorldListService.cpp
  src/engine/save/SettingsRepository.cpp
  src/engine/save/SaveOrchestrator.cpp
  src/world/generation/SimplexNoise.cpp
  src/world/generation/CaveCarver.cpp
  src/world/generation/OreDistributor.cpp
  src/world/generation/WorldGenPipeline.cpp
  src/engine/workers/mesher/GreedyMesher.cpp
  src/content/biomes/BiomeSampler.cpp
  src/content/biomes/BiomeClassifier.cpp
  src/content/biomes/BiomeFactory.cpp
  src/content/flora/FloraRegistry.cpp
  src/content/flora/GrassFactory.cpp
  src/content/flora/DefaultFlora.cpp
  src/game/GameOrchestrator.cpp
  src/game/ChunkWorkerImpl.cpp
  src/game/GameSession.cpp
  src/game/WorldController.cpp
  src/game/Game.cpp
  src/ui/UIManager.cpp
)

target_include_directories(voxel_engine PUBLIC src ${SQLITE3_INCLUDE_DIRS})

target_link_libraries(voxel_engine PUBLIC
  glm::glm
  ${GLFW_TARGET}
  OpenGL::GL
  imgui
  ${SQLITE3_LIBRARIES}
)

# Precompiled headers (per-target to avoid -Winvalid-pch due to differing flags)
target_precompile_headers(voxel_engine PRIVATE src/pch.hpp)

# Release-specific optimizations (global — keeps PCH flags consistent across all targets)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  # Use release-grade optimization flags while keeping PCH behavior stable.
  add_compile_options(-O3 -mtune=native)
endif()

# ---- Main executable ----
add_executable(voxel_app src/main.cpp)
target_link_libraries(voxel_app PRIVATE voxel_engine ${GLFW_TARGET} OpenGL::GL imgui)
target_precompile_headers(voxel_app PRIVATE src/pch.hpp)

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
  tests/test_vanilla_block_factory.cpp
  tests/test_flora.cpp
  tests/test_world.cpp
  tests/test_simplex_noise.cpp
  tests/test_biome_sampler.cpp
  tests/test_worldgen.cpp
  tests/test_save_manager.cpp
  tests/test_save_services.cpp
  tests/test_thread_pool.cpp
  tests/test_player_controller.cpp
  tests/test_mesher.cpp
)
target_link_libraries(voxel_tests PRIVATE voxel_engine Catch2::Catch2WithMain)
target_precompile_headers(voxel_tests PRIVATE src/pch.hpp)
catch_discover_tests(voxel_tests)
