# Keep chunk visibility stable during startup

## What happened
New world rendering could still end up with no visible chunks when the indirect culling stage suppressed all draw calls.

## Fix
- Add a slot-index guard in `Renderer::syncChunks` so invalid chunk slot indices
  are never written into GPU upload/cull buffers.
- Make the compute cull dispatch initialize every command entry.
- Rebuild indirect commands on the CPU after dispatch as a safety net so
  `indexCount > 0` chunks still render even if the GPU cull path is flaky.

## Why this matters
This prevents a full zero-draw-frame when culling logic or slot mapping issues
interfere, so generated terrain remains visible while we confirm the full
frustum/slot pipeline again.
