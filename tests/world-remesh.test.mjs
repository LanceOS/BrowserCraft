import assert from "node:assert/strict";
import test from "node:test";

import { SharedPool } from "../dist/engine/alloc/SharedPool.js";
import { World } from "../dist/world/World.js";

const createWorld = () => {
  const chunkSize = 4;
  const worldHeight = 8;
  const pool = SharedPool.create(1, {
    sizeX: chunkSize,
    sizeY: worldHeight,
    sizeZ: chunkSize,
    maxVertsPerChunk: 64,
    maxIndicesPerChunk: 96,
    vertexStrideFloats: 10,
  });
  const createWorker = () => ({
    onmessage: null,
    postMessage() {},
    terminate() {},
  });
  const mesherHandle = { id: 0, kind: "mesher", busy: true, worker: createWorker() };
  const world = new World(
    pool,
    [{ id: 0, kind: "worldgen", busy: false, worker: createWorker() }],
    [mesherHandle],
    { tryGet() { return null; } },
    {
      renderDistance: 0,
      chunkSize,
      worldHeight,
      maxConcurrentGenJobs: 1,
      maxConcurrentMeshJobs: 1,
      textureArrayLayers: 1,
      targetFps: 60,
      fovDegrees: 70,
      worldSeed: 123,
      maxVertsPerChunk: 64,
      maxIndicesPerChunk: 96,
    },
  );

  world.update([0, 0, 0]);
  const chunk = world.getChunk(0, 0);
  assert.ok(chunk);
  return { world, chunk, mesherHandle };
};

test("world requeues a chunk when edits arrive during meshing", () => {
  const { world, chunk, mesherHandle } = createWorld();
  chunk.state = "meshing";

  world.requestRemesh(chunk);
  assert.equal(chunk.needsRemesh, true);

  world.onMeshDone(mesherHandle, {
    kind: "meshed",
    slotIndex: chunk.slotIndex,
    success: true,
    vertexCount: 12,
    indexCount: 18,
  });

  assert.equal(chunk.needsRemesh, false);
  assert.equal(chunk.state, "queuedMesh");
});
