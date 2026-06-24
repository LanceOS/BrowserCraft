import { BiomeRegistry } from "../../../content/biomes/BiomeRegistry.js";
import type { BiomeSurfaceRule } from "../../../content/biomes/BiomeSurfaceRule.js";
import { SimplexNoise } from "./SimplexNoise.js";

const normalizeNoise = (value: number): number => (value + 1) * 0.5;

export class BiomeSampler {
  private readonly tempNoise: SimplexNoise;
  private readonly humidNoise: SimplexNoise;

  constructor(seed: number, private readonly registry: BiomeRegistry) {
    this.tempNoise = new SimplexNoise(seed ^ 0xa10be);
    this.humidNoise = new SimplexNoise(seed ^ 0xb1d07);
  }

  noise2D(x: number, z: number): number {
    return this.tempNoise.noise3D(x, 0, z);
  }

  sampleBiome(worldX: number, worldZ: number): BiomeSurfaceRule {
    const temperature = normalizeNoise(this.tempNoise.noise3D(worldX * 0.008, 0, worldZ * 0.008));
    const humidity = normalizeNoise(this.humidNoise.noise3D(worldX * 0.008, 100, worldZ * 0.008));
    return this.registry.pick(temperature, humidity);
  }
}
