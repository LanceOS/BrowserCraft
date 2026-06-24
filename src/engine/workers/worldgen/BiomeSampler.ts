import { SimplexNoise } from "./SimplexNoise.js";

export const enum BiomeId {
  OCEAN,
  PLAINS,
  FOREST,
  DESERT,
  MOUNTAINS,
}

export interface BiomeSurfaceRule {
  readonly topBlock: number;
  readonly fillerBlock: number;
  readonly depth: number;
}

const BIOME_RULES: readonly BiomeSurfaceRule[] = [
  { topBlock: 12, fillerBlock: 12, depth: 3 },
  { topBlock: 2, fillerBlock: 3, depth: 4 },
  { topBlock: 2, fillerBlock: 3, depth: 6 },
  { topBlock: 12, fillerBlock: 12, depth: 4 },
  { topBlock: 1, fillerBlock: 1, depth: 8 },
] as const;

export class BiomeSampler {
  private readonly tempNoise: SimplexNoise;
  private readonly humidNoise: SimplexNoise;

  constructor(seed: number) {
    this.tempNoise = new SimplexNoise(seed ^ 0xa10be);
    this.humidNoise = new SimplexNoise(seed ^ 0xb1d07);
  }

  noise2D(x: number, z: number): number {
    return this.tempNoise.noise3D(x, 0, z);
  }

  sampleBiome(worldX: number, worldZ: number): BiomeId {
    const temp = this.tempNoise.noise3D(worldX * 0.008, 0, worldZ * 0.008);
    const humid = this.humidNoise.noise3D(worldX * 0.008, 100, worldZ * 0.008);

    if (temp < -0.3) return BiomeId.MOUNTAINS;
    if (temp > 0.4 && humid < 0) return BiomeId.DESERT;
    if (humid > 0.3) return BiomeId.FOREST;
    if (temp < -0.2 && humid < -0.2) return BiomeId.OCEAN;
    return BiomeId.PLAINS;
  }

  getRule(id: BiomeId): BiomeSurfaceRule {
    return BIOME_RULES[id];
  }
}
