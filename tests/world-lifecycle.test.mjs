import assert from "node:assert/strict";
import test from "node:test";

import { ChunkSlotStatus, SharedPool } from "../dist/engine/alloc/SharedPool.js";
import { World } from "../dist/world/World.js";

const config = {
  renderDistance: 0,
  chunkSize: 4,
  worldHeight: 8,
  maxConcurrentGenJobs: 1,
  maxConcurrentMeshJobs: 1,
  textureArrayLayers: 1,
  targetFps: 60,
  fovDegrees: 70,
  worldSeed: 123,
  maxVertsPerChunk: 64,
  maxIndicesPerChunk: 96,
};

const createWorkerHandle = (kind) => {
  const messages = [];
  return {
    id: 0,
    kind,
    busy: false,
    messages,
    worker: {
      onmessage: null,
      postMessage(message) {
        messages.push(message);
      },
      terminate() {},
    },
  };
};

const createWorld = () => {
  const pool = SharedPool.create(4, {
    sizeX: config.chunkSize,
    sizeY: config.worldHeight,
    sizeZ: config.chunkSize,
    maxVertsPerChunk: config.maxVertsPerChunk,
    maxIndicesPerChunk: config.maxIndicesPerChunk,
    vertexStrideFloats: 10,
  });
  const worldGenHandle = createWorkerHandle("worldgen");
  const mesherHandle = createWorkerHandle("mesher");
  const world = new World(
    pool,
    [worldGenHandle],
    [mesherHandle],
    { tryGet() { return null; } },
    config,
  );
  return { pool, world, worldGenHandle, mesherHandle };
};

test("world advances chunks through generation, meshing, upload, and unload", () => {
  const { pool, world, worldGenHandle, mesherHandle } = createWorld();

  world.update([0, 0, 0]);
  const chunk = world.getChunk(0, 0);
  assert.ok(chunk);
  assert.equal(chunk.state, "generating");
  assert.equal(worldGenHandle.busy, true);
  assert.equal(worldGenHandle.messages[0].kind, "generate");

  world.onWorldGenDone(worldGenHandle, {
    kind: "generated",
    slotIndex: chunk.slotIndex,
    chunkX: 0,
    chunkZ: 0,
  });
  assert.equal(worldGenHandle.busy, false);
  assert.equal(chunk.state, "voxelsReady");

  world.update([0, 0, 0]);
  assert.equal(chunk.state, "meshing");
  assert.equal(mesherHandle.busy, true);
  assert.equal(mesherHandle.messages[0].kind, "mesh");

  world.onMeshDone(mesherHandle, {
    kind: "meshed",
    slotIndex: chunk.slotIndex,
    success: true,
    vertexCount: 12,
    indexCount: 18,
  });
  assert.equal(mesherHandle.busy, false);
  assert.equal(chunk.state, "meshReady");

  world.markUploaded(chunk);
  assert.equal(chunk.state, "uploaded");
  assert.equal(Atomics.load(pool.view(chunk.slotIndex).status, 0), ChunkSlotStatus.GPU_UPLOADED);

  world.update([config.chunkSize * 2, 0, 0]);
  assert.equal(world.getChunk(0, 0), undefined);

  world.dispose();
});
