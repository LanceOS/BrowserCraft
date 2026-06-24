import type { ChunkDimensions } from "../../alloc/SharedPool.js";
import { MAX_LIGHT, getBlockLight, getSkyLight, packLight } from "./LightPacker.js";

const NEIGHBOR_OFFSETS = [
  [1, 0, 0],
  [-1, 0, 0],
  [0, 1, 0],
  [0, -1, 0],
  [0, 0, 1],
  [0, 0, -1],
] as const;

export class LightPropagator {
  static calculate(
    voxels: Uint8Array,
    light: Uint8Array,
    dims: ChunkDimensions,
    getEmission: (blockId: number, index: number) => number,
    transparentBlocks: Set<number>,
  ): void {
    const { sizeX, sizeY, sizeZ } = dims;
    const queue = new Int32Array(sizeX * sizeY * sizeZ * 3);
    let head = 0;
    let tail = 0;

    light.fill(0);

    for (let z = 0; z < sizeZ; z++) {
      for (let x = 0; x < sizeX; x++) {
        let skyLevel = MAX_LIGHT;
        for (let y = sizeY - 1; y >= 0; y--) {
          const idx = (y * sizeZ + z) * sizeX + x;
          const blockId = voxels[idx];
          if (blockId !== 0 && !transparentBlocks.has(blockId)) {
            skyLevel = 0;
          }

          const emitted = getEmission(blockId, idx);
          light[idx] = packLight(skyLevel, emitted);
          if (emitted > 1) {
            queue[tail++] = x;
            queue[tail++] = y;
            queue[tail++] = z;
          }
        }
      }
    }

    while (head < tail) {
      const x = queue[head++];
      const y = queue[head++];
      const z = queue[head++];
      const idx = (y * sizeZ + z) * sizeX + x;
      const packed = light[idx];
      const blockLight = getBlockLight(packed);
      if (blockLight <= 1) continue;

      const nextLight = blockLight - 1;
      for (const [dx, dy, dz] of NEIGHBOR_OFFSETS) {
        const nx = x + dx;
        const ny = y + dy;
        const nz = z + dz;
        if (nx < 0 || nx >= sizeX || ny < 0 || ny >= sizeY || nz < 0 || nz >= sizeZ) continue;

        const nextIdx = (ny * sizeZ + nz) * sizeX + nx;
        const nextBlock = voxels[nextIdx];
        if (nextBlock !== 0 && !transparentBlocks.has(nextBlock)) continue;

        const current = light[nextIdx];
        if (getBlockLight(current) >= nextLight) continue;
        light[nextIdx] = packLight(getSkyLight(current), nextLight);
        queue[tail++] = nx;
        queue[tail++] = ny;
        queue[tail++] = nz;
      }
    }
  }
}
