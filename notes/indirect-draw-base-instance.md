# Use `baseInstance` for chunk translation in indirect drawing

## What happened
The chunk renderer submits one indirect draw per chunk slot and stores the slot index in `baseInstance`.

The vertex shader was reading `gl_InstanceID` instead of the draw's `baseInstance`, so every draw could sample the wrong chunk translation and stack geometry onto a single slot.

## Fix
- Read the chunk translation using `gl_BaseInstance`.
- Keep `baseInstance` aligned with the chunk slot index in the indirect command buffer.

## Why this matters
Indirect chunk draws now render each chunk in its own world-space position instead of collapsing the entire world into the first slot.
