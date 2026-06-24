import assert from "node:assert/strict";
import test from "node:test";

import {
  handleSaveWorkerMessage,
  localChunkKey,
  regionKey,
} from "../dist/engine/save/SaveWorkerCore.js";

class MemoryRegionStore {
  regions = new Map();

  async getRegion(id) {
    return this.regions.get(id);
  }

  async putRegion(record) {
    this.regions.set(record.id, record);
  }
}

test("save worker core stores chunks by region and local chunk key", async () => {
  const store = new MemoryRegionStore();
  const raw = new Uint8Array([1, 1, 1, 2, 3, 3, 3, 4]);

  const saved = await handleSaveWorkerMessage({
    type: "SAVE_CHUNK",
    worldId: "world",
    chunkX: -1,
    chunkZ: 34,
    regionX: -1,
    regionZ: 1,
    rawBuffer: raw.buffer,
  }, store);

  assert.equal(saved.message.type, "SAVE_COMPLETE");
  assert.equal(localChunkKey(-1, 34), "31:2");

  const region = store.regions.get(regionKey("world", -1, 1));
  assert.ok(region);
  assert.ok(region.chunks["31:2"]);
  assert.equal(region.chunks["31:2"].rawSize, raw.byteLength);
});

test("save worker core round-trips chunk payloads through RLE", async () => {
  const store = new MemoryRegionStore();
  const raw = new Uint8Array([7, 7, 7, 7, 9, 10, 11, 11, 11]);

  await handleSaveWorkerMessage({
    type: "SAVE_CHUNK",
    worldId: "world",
    chunkX: 3,
    chunkZ: 4,
    regionX: 0,
    regionZ: 0,
    rawBuffer: raw.buffer,
  }, store);

  const loaded = await handleSaveWorkerMessage({
    type: "LOAD_CHUNK",
    worldId: "world",
    chunkX: 3,
    chunkZ: 4,
    regionX: 0,
    regionZ: 0,
  }, store);

  assert.equal(loaded.message.type, "LOAD_SUCCESS");
  assert.deepEqual(Array.from(new Uint8Array(loaded.message.buffer)), Array.from(raw));
  assert.deepEqual(loaded.transfer, [loaded.message.buffer]);
});

test("save worker core reports missing and corrupt records", async () => {
  const store = new MemoryRegionStore();

  const missing = await handleSaveWorkerMessage({
    type: "LOAD_CHUNK",
    worldId: "world",
    chunkX: 8,
    chunkZ: 9,
    regionX: 0,
    regionZ: 0,
  }, store);
  assert.equal(missing.message.type, "LOAD_FAILED");

  await store.putRegion({
    id: regionKey("world", 0, 0),
    chunks: {
      [localChunkKey(8, 9)]: {
        rawSize: 4,
        buffer: new Uint8Array([0]).buffer,
      },
    },
  });

  const corrupt = await handleSaveWorkerMessage({
    type: "LOAD_CHUNK",
    worldId: "world",
    chunkX: 8,
    chunkZ: 9,
    regionX: 0,
    regionZ: 0,
  }, store);
  assert.equal(corrupt.message.type, "LOAD_ERROR");
  assert.match(corrupt.message.reason, /RLE payload/);
});
