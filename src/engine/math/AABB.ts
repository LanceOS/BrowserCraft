export interface AABB {
  readonly minX: number;
  readonly minY: number;
  readonly minZ: number;
  readonly maxX: number;
  readonly maxY: number;
  readonly maxZ: number;
}

export const aabbFromChunk = (chunkX: number, chunkZ: number, sizeX: number, sizeY: number, sizeZ: number): AABB => ({
  minX: chunkX * sizeX,
  minY: 0,
  minZ: chunkZ * sizeZ,
  maxX: chunkX * sizeX + sizeX,
  maxY: sizeY,
  maxZ: chunkZ * sizeZ + sizeZ,
});
