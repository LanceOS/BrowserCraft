# ImGui Manual GLFW Callback Forwarding for Gameplay + UI Input

## What happened
GUI interaction still broke intermittently when ImGui and gameplay shared GLFW callbacks.

## Root cause
The previous setup relied on callback chaining after `ImGui_ImplGlfw_InitForOpenGL(..., true)`. Even though chaining should work, it kept being sensitive to registration order and to window/input-mode transitions, which could leave ImGui and game input out of sync.

## Fix
`ImGui_ImplGlfw_InitForOpenGL` now runs with `install_callbacks = false` in `UIManager`.

`src/main.cpp` now installs a single explicit callback set after the `Game`/ImGui context is created:

- key events
- cursor position
- mouse buttons
- scroll
- char input
- window focus
- cursor enter/leave

Each callback updates `InputState` where needed and forwards the event to `ImGui_ImplGlfw_*` so both the game systems and ImGui receive input.

## Why this matters
This keeps UI buttons and camera/controls consistently responsive regardless of menu state, cursor-mode transitions, and backend callback ordering.
