#pragma once

struct GLFWwindow;

namespace voxel {

class InputState;

/// Install the GLFW callbacks that forward events into InputState and ImGui.
void setupInputCallbacks(GLFWwindow* window, InputState& input);

/// Clear the callback bridge's cached input pointer.
void clearInputCallbacks();

} // namespace voxel
