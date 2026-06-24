import { DesertBiome } from "./Desert.js";
import { ForestBiome } from "./Forest.js";
import { MountainsBiome } from "./Mountains.js";
import { PlainsBiome } from "./Plains.js";
import { SwampBiome } from "./Swamp.js";
import type { BiomeSurfaceRule } from "./BiomeSurfaceRule.js";

export class BiomeRegistry {
  private readonly biomes: BiomeSurfaceRule[] = [
    PlainsBiome,
    DesertBiome,
    ForestBiome,
    MountainsBiome,
    SwampBiome,
  ];

  all(): readonly BiomeSurfaceRule[] {
    return this.biomes;
  }

  pick(temperature: number, humidity: number): BiomeSurfaceRule {
    if (temperature > 0.65 && humidity < 0.35) return DesertBiome;
    if (humidity > 0.72 && temperature > 0.35) return SwampBiome;
    if (temperature < 0.28) return MountainsBiome;
    if (humidity > 0.55) return ForestBiome;
    return PlainsBiome;
  }
}
