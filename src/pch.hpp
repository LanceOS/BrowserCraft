// Precompiled header for the terrain engine.
// Includes stable, rarely-changed, expensive-to-parse headers.
// This file is force-included by CMake's target_precompile_headers.

#pragma once

// STL heavyweights — used across almost every translation unit
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <functional>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <array>
#include <optional>
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>
#include <deque>
#include <chrono>
#include <random>
#include <cstring>
#include <cassert>
#include <stdexcept>
#include <utility>
#include <limits>

// GLM (header-only math library)
#include <glm/glm.hpp>

// GLFW (windowing + input)
#include <GLFW/glfw3.h>
