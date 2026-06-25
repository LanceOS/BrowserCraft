# Renderer indirect draw CPU command fallback

Some environments (driver/version combinations) intermittently produced an empty or invalid indirect command buffer when relying on the compute cull shader.
That made the render path draw nothing even when `ChunkCullData` contained meshable chunks.

As a defensive fallback, command entries are now rebuilt on the CPU immediately after compute dispatch:
- `count` from chunk index count
- `instanceCount` set to `1` when index count is non-zero
- `firstIndex`, `baseVertex`, `baseInstance` copied from chunk metadata

Keep this note with the `@see` in `src/engine/render/IndirectBatcher.hpp` until we can fully verify driver-level compute dispatch reliability.
