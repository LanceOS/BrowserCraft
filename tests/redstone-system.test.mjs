import assert from "node:assert/strict";
import test from "node:test";

import { SharedPool } from "../dist/engine/alloc/SharedPool.js";
import { RedstoneSystem } from "../dist/engine/ecs/systems/RedstoneSystem.js";
import { getPower, packRedstone } from "../dist/engine/workers/redstone/RedstonePacker.js";

const WIRE = 55;
const TORCH_ON = 76;
const LAMP = 123;

const indexFor = (x, y, z, sizeX = 16, sizeZ = 16) => (y * sizeZ + z) * sizeX + x;

const createHarness = () => {
  const pool = SharedPool.create(1, {
    sizeX: 16,
    sizeY: 4,
    sizeZ: 16,
    maxVertsPerChunk: 16,
    maxIndicesPerChunk: 16,
    vertexStrideFloats: 10,
  });
  const slot = pool.acquire();
  assert.ok(slot);

  const chunk = { slotIndex: slot.slotIndex, chunkX: 0, chunkZ: 0 };
  const calls = { remesh: 0, dirty: 0 };
  const world = {
    getChunkBySlotIndex(slotIndex) {
      return slotIndex === slot.slotIndex ? chunk : undefined;
    },
    requestRemesh(target) {
      assert.equal(target, chunk);
      calls.remesh++;
    },
    markChunkDirty(chunkX, chunkZ) {
      assert.equal(chunkX, 0);
      assert.equal(chunkZ, 0);
      calls.dirty++;
    },
    resolveBlock(worldX, worldY, worldZ) {
      if (worldX < 0 || worldX >= 16 || worldZ < 0 || worldZ >= 16 || worldY < 0 || worldY >= 4) return null;
      return {
        chunk,
        localX: worldX,
        localZ: worldZ,
        index: indexFor(worldX, worldY, worldZ),
      };
    },
  };

  return {
    calls,
    pool,
    slot,
    system: new RedstoneSystem(pool, world),
  };
};

test("redstone wires propagate power with distance decay", () => {
  const { slot, system } = createHarness();
  const y = 1;
  const z = 1;
  slot.voxels[indexFor(1, y, z)] = TORCH_ON;
  slot.voxels[indexFor(2, y, z)] = WIRE;
  slot.voxels[indexFor(3, y, z)] = WIRE;

  assert.equal(system.triggerAtWorld(1, y, z, 0), true);
  system.update({}, 0.1);
  assert.equal(getPower(slot.redstone[indexFor(1, y, z)]), 15);

  system.update({}, 0.1);
  assert.equal(getPower(slot.redstone[indexFor(2, y, z)]), 15);

  system.update({}, 0.1);
  assert.equal(getPower(slot.redstone[indexFor(3, y, z)]), 14);
});

test("redstone wires decay when no neighbor supplies power", () => {
  const { slot, system } = createHarness();
  const idx = indexFor(4, 1, 4);
  slot.voxels[idx] = WIRE;
  slot.redstone[idx] = packRedstone(12, 0);

  assert.equal(system.triggerAtWorld(4, 1, 4, 0), true);
  system.update({}, 0.1);

  assert.equal(getPower(slot.redstone[idx]), 0);
});

test("redstone power is bounded to four bits", () => {
  const { slot, system } = createHarness();
  const idx = indexFor(5, 1, 5);
  slot.voxels[idx] = LAMP;

  assert.equal(system.triggerAtWorld(5, 1, 5, 31), true);
  system.update({}, 0.1);

  assert.equal(getPower(slot.redstone[idx]), 15);
});
