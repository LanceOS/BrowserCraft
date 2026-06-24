export interface StructureBlueprint {
  readonly id: number;
  readonly sizeX: number;
  readonly sizeY: number;
  readonly sizeZ: number;
  readonly blocks: Uint8Array;
  readonly paletteWeight: number;
}

const toSigned = (value: number): number => (value << 24) >> 24;

export function stampBlueprint(
  voxels: Uint8Array,
  blueprint: StructureBlueprint,
  originX: number,
  originY: number,
  originZ: number,
  rotation: number,
  sizeX: number,
  sizeY: number,
  sizeZ: number,
  replaceAirOnly = false,
): void {
  const blocks = blueprint.blocks;

  for (let i = 0; i < blocks.length; i += 4) {
    const dx = toSigned(blocks[i]);
    if (dx === -128) break;
    const dy = toSigned(blocks[i + 1]);
    const dz = toSigned(blocks[i + 2]);
    const blockId = blocks[i + 3];

    let rx = dx;
    let rz = dz;
    switch (rotation & 3) {
      case 1: rx = -dz; rz = dx; break;
      case 2: rx = -dx; rz = -dz; break;
      case 3: rx = dz; rz = -dx; break;
    }

    const worldX = originX + rx;
    const worldY = originY + dy;
    const worldZ = originZ + rz;

    if (worldX < 0 || worldX >= sizeX || worldY < 0 || worldY >= sizeY || worldZ < 0 || worldZ >= sizeZ) {
      continue;
    }

    const index = (worldY * sizeZ + worldZ) * sizeX + worldX;
    if (replaceAirOnly && voxels[index] !== 0) continue;
    voxels[index] = blockId;
  }
}
