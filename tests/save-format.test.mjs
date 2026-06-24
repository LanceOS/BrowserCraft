import assert from "node:assert/strict";
import test from "node:test";

import { compressRLE, decompressRLE } from "../dist/engine/save/RLE.js";
import { SAVE_HEADER_BYTES, deserializeChunkData, serializeChunkData } from "../dist/engine/save/SaveFormat.js";

test("chunk save format round-trips voxel payloads", () => {
  const voxels = new Uint8Array([1, 2, 3, 4]);
  const light = new Uint8Array([9, 8]);
  const redstone = new Uint8Array([7, 6, 5]);

  const buffer = serializeChunkData(12, -4, voxels, light, redstone);
  const { header, voxels: outVoxels, light: outLight, redstone: outRedstone } = deserializeChunkData(buffer);

  assert.equal(header.chunkX, 12);
  assert.equal(header.chunkZ, -4);
  assert.deepEqual(Array.from(outVoxels), [1, 2, 3, 4]);
  assert.deepEqual(Array.from(outLight), [9, 8]);
  assert.deepEqual(Array.from(outRedstone), [7, 6, 5]);
});

test("deserializeChunkData rejects inconsistent payload sizes", () => {
  const buffer = serializeChunkData(0, 0, new Uint8Array([1, 2]), new Uint8Array([3]), new Uint8Array([4]));
  const view = new DataView(buffer);
  view.setUint32(16, 999, true);

  assert.throws(() => deserializeChunkData(buffer), /payload/i);
});

test("decompressRLE rejects truncated or overlong streams", () => {
  assert.throws(() => decompressRLE(new Uint8Array([0]), new Uint8Array(2)), /Corrupt RLE payload/);
  assert.throws(() => decompressRLE(new Uint8Array([0, 9, 1]), new Uint8Array(1)), /Corrupt RLE payload/);
});

test("RLE round-trips mixed literal and repeated runs", () => {
  const source = new Uint8Array([4, 4, 4, 9, 1, 2, 3, 3, 3, 3, 7]);
  const compressed = new Uint8Array(SAVE_HEADER_BYTES + source.length);
  const used = compressRLE(source, compressed);
  const restored = new Uint8Array(source.length);

  decompressRLE(compressed.subarray(0, used), restored);

  assert.deepEqual(Array.from(restored), Array.from(source));
});
