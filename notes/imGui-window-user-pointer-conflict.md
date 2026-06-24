# GLFW Input Callbacks and ImGui Window User Pointer Conflict

## What happened
During input handling setup, we previously relied on `glfwGetWindowUserPointer(window)` inside GLFW input callbacks to retrieve input state.

## Root cause
ImGui’s GLFW backend also sets/uses the GLFW window user pointer during initialization. When our code later tried to use that pointer for input state, the value could be replaced, causing callback handlers to get a null/invalid context.

## Fix
Instead of reading input context from the window user pointer, we now store the input context in a process-local global pointer used by the callbacks:

- `g_inputContext` points to the active `CallbackContext`
- callbacks validate `g_inputContext` and forward mouse/keyboard state into `InputState`
- callbacks keep plain GLFW-compatible signatures (no captures)

This avoids dependency on window user pointer ownership and restores reliable input dispatch.

## Why this matters
It prevents "window not responding / no movement/input" regressions when UI systems (like ImGui) also need GLFW internals.

