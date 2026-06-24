import { Tex } from "../../world/blocks/TextureLayers.js";

export interface TextureLayerData {
  readonly layer: number;
  readonly width: number;
  readonly height: number;
  readonly pixels: Uint8Array;
}

interface LayerColor {
  readonly r: number;
  readonly g: number;
  readonly b: number;
  readonly a?: number;
  readonly accent?: number;
}

const PLACEHOLDER_SIZE = 16;
const FALLBACK_COLOR: LayerColor = { r: 255, g: 0, b: 255 };

const LAYER_COLORS: Record<number, LayerColor> = {
  [Tex.AIR]: { r: 120, g: 186, b: 96, a: 0 },
  [Tex.STONE]: { r: 126, g: 132, b: 138 },
  [Tex.GRASS_TOP]: { r: 98, g: 158, b: 78 },
  [Tex.GRASS_SIDE]: { r: 92, g: 120, b: 58 },
  [Tex.DIRT]: { r: 126, g: 84, b: 58 },
  [Tex.COBBLESTONE]: { r: 107, g: 110, b: 114 },
  [Tex.PLANKS_OAK]: { r: 182, g: 151, b: 82 },
  [Tex.LOG_OAK_TOP]: { r: 149, g: 119, b: 71 },
  [Tex.LOG_OAK_SIDE]: { r: 108, g: 82, b: 51 },
  [Tex.LEAVES_OAK]: { r: 90, g: 152, b: 76, a: 180, accent: 3 },
  [Tex.GLASS]: { r: 204, g: 232, b: 255, a: 118, accent: 2 },
  [Tex.SAND]: { r: 210, g: 194, b: 126 },
  [Tex.GRAVEL]: { r: 136, g: 131, b: 126 },
  [Tex.COAL_ORE]: { r: 112, g: 112, b: 116 },
  [Tex.IRON_ORE]: { r: 166, g: 136, b: 108 },
  [Tex.GOLD_ORE]: { r: 186, g: 154, b: 74 },
  [Tex.DIAMOND_ORE]: { r: 92, g: 182, b: 184 },
  [Tex.REDSTONE_ORE]: { r: 150, g: 56, b: 50 },
  [Tex.LAPIS_ORE]: { r: 72, g: 96, b: 176 },
  [Tex.BEDROCK]: { r: 84, g: 84, b: 84 },
  [Tex.WATER]: { r: 72, g: 134, b: 210, a: 168, accent: 1 },
  [Tex.LAVA]: { r: 224, g: 92, b: 32, a: 210, accent: 1 },
  [Tex.BRICK]: { r: 156, g: 78, b: 66 },
  [Tex.MOSSY_COBBLESTONE]: { r: 95, g: 114, b: 82 },
  [Tex.OBSIDIAN]: { r: 44, g: 32, b: 62 },
  [Tex.CACTUS_TOP]: { r: 72, g: 128, b: 54 },
  [Tex.CACTUS_SIDE]: { r: 64, g: 142, b: 56 },
  [Tex.CACTUS_BOTTOM]: { r: 96, g: 82, b: 54 },
  [Tex.GLOWSTONE]: { r: 226, g: 190, b: 110 },
  [Tex.GOLD_BLOCK]: { r: 238, g: 214, b: 80 },
  [Tex.IRON_BLOCK]: { r: 212, g: 214, b: 216 },
  [Tex.DIAMOND_BLOCK]: { r: 98, g: 224, b: 214 },
  [Tex.CRAFTING_TABLE_TOP]: { r: 160, g: 132, b: 74 },
  [Tex.CRAFTING_TABLE_SIDE]: { r: 122, g: 84, b: 50 },
  [Tex.FURNACE_SIDE]: { r: 122, g: 122, b: 122 },
  [Tex.FURNACE_TOP]: { r: 96, g: 96, b: 96 },
  [Tex.SANDSTONE]: { r: 218, g: 198, b: 136 },
};

for (let power = 0; power < 16; power++) {
  const brightness = 72 + power * 10;
  LAYER_COLORS[Tex.REDSTONE_WIRE_0 + power] = {
    r: brightness,
    g: 18 + power * 4,
    b: 18 + power * 2,
    a: 220,
    accent: power,
  };
}

LAYER_COLORS[Tex.REDSTONE_LAMP_OFF] = { r: 86, g: 62, b: 32 };
LAYER_COLORS[Tex.REDSTONE_LAMP_ON] = { r: 242, g: 200, b: 108 };

const clampByte = (value: number): number => Math.max(0, Math.min(255, value));

export class TexturePipeline {
  readonly width = PLACEHOLDER_SIZE;
  readonly height = PLACEHOLDER_SIZE;

  createLayerData(layer: number): TextureLayerData {
    const color = LAYER_COLORS[layer] ?? FALLBACK_COLOR;
    return {
      layer,
      width: this.width,
      height: this.height,
      pixels: this.makePlaceholderLayer(color),
    };
  }

  createLayerArray(layerCount: number): TextureLayerData[] {
    return Array.from({ length: layerCount }, (_, layer) => this.createLayerData(layer));
  }

  private makePlaceholderLayer({ r, g, b, a = 255, accent = 0 }: LayerColor): Uint8Array {
    const data = new Uint8Array(this.width * this.height * 4);
    for (let y = 0; y < this.height; y++) {
      for (let x = 0; x < this.width; x++) {
        const index = (y * this.width + x) * 4;
        const tint = ((x + y + accent) & 1) === 0 ? 10 : -10;
        data[index + 0] = clampByte(r + tint);
        data[index + 1] = clampByte(g + tint);
        data[index + 2] = clampByte(b + tint);
        data[index + 3] = a;
      }
    }
    return data;
  }
}
