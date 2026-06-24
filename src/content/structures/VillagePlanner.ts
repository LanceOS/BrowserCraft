import type { StructureBlueprint } from "./StructureBlueprint.js";
import { stampBlueprint } from "./StructureBlueprint.js";

function mulberry32(seed: number): () => number {
  let s = seed >>> 0;
  return () => {
    s = (s + 0x6d2b79f5) | 0;
    let t = Math.imul(s ^ (s >>> 15), 1 | s);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export class VillagePlanner {
  constructor(private readonly blueprints: readonly StructureBlueprint[]) {}

  generate(
    regionX: number,
    regionZ: number,
    chunkX: number,
    chunkZ: number,
    voxels: Uint8Array,
    dims: { sizeX: number; sizeY: number; sizeZ: number },
  ): void {
    const regionSeed = Math.imul(regionX, 73856093) ^ Math.imul(regionZ, 19349663);
    const rng = mulberry32(regionSeed);
    if (rng() > 0.25) return;

    const centerRegionX = Math.floor(rng() * 64);
    const centerRegionZ = Math.floor(rng() * 64);
    const worldCenterX = regionX * 64 + centerRegionX;
    const worldCenterZ = regionZ * 64 + centerRegionZ;
    const baseHeight = 64;

    for (let i = 0; i < 8; i++) {
      const angle = rng() * Math.PI * 2;
      const dist = 5 + Math.floor(rng() * 15);
      const compWorldX = Math.floor(worldCenterX + Math.cos(angle) * dist);
      const compWorldZ = Math.floor(worldCenterZ + Math.sin(angle) * dist);
      const rotation = Math.floor(rng() * 4);
      const blueprint = this.blueprints[Math.floor(rng() * this.blueprints.length)];
      this.tryStampComponent(compWorldX, baseHeight, compWorldZ, rotation, blueprint, chunkX, chunkZ, voxels, dims);
    }

    const well = this.blueprints.find((blueprint) => blueprint.id === 0);
    if (well) {
      this.tryStampComponent(worldCenterX, baseHeight, worldCenterZ, 0, well, chunkX, chunkZ, voxels, dims);
    }
  }

  private tryStampComponent(
    compWorldX: number,
    compWorldY: number,
    compWorldZ: number,
    rotation: number,
    blueprint: StructureBlueprint,
    chunkX: number,
    chunkZ: number,
    voxels: Uint8Array,
    dims: { sizeX: number; sizeY: number; sizeZ: number },
  ): void {
    const localOriginX = compWorldX - chunkX * dims.sizeX;
    const localOriginZ = compWorldZ - chunkZ * dims.sizeZ;
    const maxLocalX = localOriginX + blueprint.sizeX;
    const maxLocalZ = localOriginZ + blueprint.sizeZ;

    if (maxLocalX < 0 || localOriginX >= dims.sizeX || maxLocalZ < 0 || localOriginZ >= dims.sizeZ) {
      return;
    }

    stampBlueprint(
      voxels,
      blueprint,
      localOriginX,
      compWorldY,
      localOriginZ,
      rotation,
      dims.sizeX,
      dims.sizeY,
      dims.sizeZ,
      false,
    );
  }
}
