import assert from "node:assert/strict";
import test from "node:test";

import { SharedPool } from "../dist/engine/alloc/SharedPool.js";
import { greedyMeshChunk } from "../dist/engine/workers/mesher/GreedyMesher.js";
import { getSkyLight } from "../dist/engine/workers/mesher/LightPacker.js";
import { LightPropagator } from "../dist/engine/workers/mesher/LightPropagator.js";
import { BlockRegistry } from "../dist/world/BlockRegistry.js";
import { VanillaBlockFactory } from "../dist/world/BlockFactory.js";

test("greedy mesher preserves sky light on chunk-edge top faces", () => {
  const dims = {
    sizeX: 4,
    sizeY: 4,
    sizeZ: 4,
    maxVertsPerChunk: 256,
    maxIndicesPerChunk: 384,
    vertexStrideFloats: 10,
  };
  const pool = SharedPool.create(1, dims);
  const slot = pool.acquire();
  assert.ok(slot);

  const blocks = new BlockRegistry(4096);
  new VanillaBlockFactory().registerAll(blocks);

  for (let z = 0; z < dims.sizeZ; z++) {
    for (let x = 0; x < dims.sizeX; x++) {
      slot.voxels[z * dims.sizeX + x] = 1;
    }
  }

  LightPropagator.calculate(slot.voxels, slot.light, dims, () => 0, new Set([0]));
  assert.equal(greedyMeshChunk(slot, dims, blocks), true);

  let topVertexCount = 0;
  for (let vertex = 0; vertex < slot.vertexCount[0]; vertex++) {
    const offset = vertex * dims.vertexStrideFloats;
    if (slot.vertices[offset + 4] !== 1) continue;
    topVertexCount++;
    assert.equal(getSkyLight(slot.vertices[offset + 9]), 15);
  }

  assert.equal(topVertexCount, 4);
});

test("greedy mesher culls boundary faces against ready neighbor chunks", () => {
  const dims = {
    sizeX: 4,
    sizeY: 4,
    sizeZ: 4,
    maxVertsPerChunk: 256,
    maxIndicesPerChunk: 384,
    vertexStrideFloats: 10,
  };
  const pool = SharedPool.create(2, dims);
  const slot = pool.acquire();
  const posX = pool.acquire();
  assert.ok(slot);
  assert.ok(posX);

  const blocks = new BlockRegistry(4096);
  new VanillaBlockFactory().registerAll(blocks);

  for (let z = 0; z < dims.sizeZ; z++) {
    for (let x = 0; x < dims.sizeX; x++) {
      slot.voxels[z * dims.sizeX + x] = 1;
      posX.voxels[z * dims.sizeX + x] = 1;
    }
  }

  LightPropagator.calculate(slot.voxels, slot.light, dims, () => 0, new Set([0]));
  LightPropagator.calculate(posX.voxels, posX.light, dims, () => 0, new Set([0]));
  assert.equal(greedyMeshChunk(slot, dims, blocks, { posX }), true);

  for (let vertex = 0; vertex < slot.vertexCount[0]; vertex++) {
    const offset = vertex * dims.vertexStrideFloats;
    assert.notEqual(slot.vertices[offset + 3], 1);
  }
});
