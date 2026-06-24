interface OreConfig {
  readonly blockId: number;
  readonly minY: number;
  readonly maxY: number;
  readonly veinsPerChunk: number;
  readonly veinSize: number;
}

const ORE_CONFIGS: readonly OreConfig[] = [
  { blockId: 16, minY: 5, maxY: 64, veinsPerChunk: 20, veinSize: 8 },
  { blockId: 15, minY: 5, maxY: 32, veinsPerChunk: 10, veinSize: 6 },
  { blockId: 14, minY: 5, maxY: 16, veinsPerChunk: 4, veinSize: 4 },
  { blockId: 56, minY: 5, maxY: 12, veinsPerChunk: 2, veinSize: 4 },
] as const;

export class OreDistributor {
  private readonly rng: () => number;

  constructor(seed: number) {
    let s = seed ^ 0x0be5;
    this.rng = () => {
      s = (Math.imul(s, 48271) + 1) | 0;
      return (s >>> 0) / 4294967296;
    };
  }

  distribute(voxels: Uint8Array, sizeX: number, sizeY: number, sizeZ: number): void {
    for (const cfg of ORE_CONFIGS) {
      for (let vein = 0; vein < cfg.veinsPerChunk; vein++) {
        let x = Math.floor(this.rng() * sizeX);
        let y = cfg.minY + Math.floor(this.rng() * (cfg.maxY - cfg.minY));
        let z = Math.floor(this.rng() * sizeZ);

        for (let step = 0; step < cfg.veinSize; step++) {
          x += this.rng() > 0.5 ? 1 : -1;
          y += this.rng() > 0.5 ? 1 : -1;
          z += this.rng() > 0.5 ? 1 : -1;
          if (x < 0 || x >= sizeX || y < 0 || y >= sizeY || z < 0 || z >= sizeZ) continue;
          const index = (y * sizeZ + z) * sizeX + x;
          if (voxels[index] === 1) voxels[index] = cfg.blockId;
        }
      }
    }
  }
}
