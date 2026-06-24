import { BlockRegistry } from "./BlockRegistry.js";
import type { BlockDefinition } from "./blocks/BlockDefinition.js";
import { FULL_BLOCK_AABB } from "./blocks/AABB.js";
import { Tex } from "./blocks/TextureLayers.js";

export interface BlockFactory {
  registerAll(registry: BlockRegistry): void;
}

export class VanillaBlockFactory implements BlockFactory {
  registerAll(registry: BlockRegistry): void {
    const EMPTY_AABB = { minX: 0, minY: 0, minZ: 0, maxX: 0, maxY: 0, maxZ: 0 } as const;
    const opaque = (
      id: number,
      name: string,
      textures: BlockDefinition["textures"],
      collision = FULL_BLOCK_AABB,
    ): void => {
      registry.register({
        id,
        name,
        textures,
        material: { opaque: true, transparent: false, liquid: false, foliage: false, lightEmission: 0 },
        collision,
      });
    };
    const transparent = (
      id: number,
      name: string,
      textures: BlockDefinition["textures"],
      foliage = false,
      collision = FULL_BLOCK_AABB,
    ): void => {
      registry.register({
        id,
        name,
        textures,
        material: { opaque: false, transparent: true, liquid: false, foliage, lightEmission: 0 },
        collision,
      });
    };
    const liquid = (id: number, name: string, texture: number, lightEmission = 0): void => {
      registry.register({
        id,
        name,
        textures: { top: texture, bottom: texture, side: texture },
        material: { opaque: false, transparent: true, liquid: true, foliage: false, lightEmission },
        collision: EMPTY_AABB,
      });
    };
    const emitter = (
      id: number,
      name: string,
      textures: BlockDefinition["textures"],
      lightEmission: number,
    ): void => {
      registry.register({
        id,
        name,
        textures,
        material: { opaque: true, transparent: false, liquid: false, foliage: false, lightEmission },
        collision: FULL_BLOCK_AABB,
      });
    };

    opaque(1, "stone", { top: Tex.STONE, bottom: Tex.STONE, side: Tex.STONE });
    opaque(2, "grass", { top: Tex.GRASS_TOP, bottom: Tex.DIRT, side: Tex.GRASS_SIDE });
    opaque(3, "dirt", { top: Tex.DIRT, bottom: Tex.DIRT, side: Tex.DIRT });
    opaque(4, "cobblestone", { top: Tex.COBBLESTONE, bottom: Tex.COBBLESTONE, side: Tex.COBBLESTONE });
    opaque(5, "wood_planks", { top: Tex.PLANKS_OAK, bottom: Tex.PLANKS_OAK, side: Tex.PLANKS_OAK });
    opaque(7, "bedrock", { top: Tex.BEDROCK, bottom: Tex.BEDROCK, side: Tex.BEDROCK });
    liquid(8, "water", Tex.WATER, 0);
    liquid(9, "still_water", Tex.WATER, 0);
    liquid(10, "lava", Tex.LAVA, 15);
    opaque(12, "sand", { top: Tex.SAND, bottom: Tex.SAND, side: Tex.SAND });
    opaque(13, "gravel", { top: Tex.GRAVEL, bottom: Tex.GRAVEL, side: Tex.GRAVEL });
    opaque(14, "gold_ore", { top: Tex.GOLD_ORE, bottom: Tex.GOLD_ORE, side: Tex.GOLD_ORE });
    opaque(15, "iron_ore", { top: Tex.IRON_ORE, bottom: Tex.IRON_ORE, side: Tex.IRON_ORE });
    opaque(16, "coal_ore", { top: Tex.COAL_ORE, bottom: Tex.COAL_ORE, side: Tex.COAL_ORE });
    opaque(17, "log", { top: Tex.LOG_OAK_TOP, bottom: Tex.LOG_OAK_TOP, side: Tex.LOG_OAK_SIDE });
    transparent(18, "leaves", { top: Tex.LEAVES_OAK, bottom: Tex.LEAVES_OAK, side: Tex.LEAVES_OAK }, true);
    transparent(20, "glass", { top: Tex.GLASS, bottom: Tex.GLASS, side: Tex.GLASS });
    opaque(21, "lapis_ore", { top: Tex.LAPIS_ORE, bottom: Tex.LAPIS_ORE, side: Tex.LAPIS_ORE });
    opaque(22, "lapis_block", { top: Tex.LAPIS_BLOCK, bottom: Tex.LAPIS_BLOCK, side: Tex.LAPIS_BLOCK });
    opaque(23, "sandstone", { top: Tex.SANDSTONE, bottom: Tex.SANDSTONE, side: Tex.SANDSTONE });
    registry.register({
      id: 24,
      name: "cactus",
      textures: { top: Tex.CACTUS_TOP, bottom: Tex.CACTUS_BOTTOM, side: Tex.CACTUS_SIDE },
      material: { opaque: true, transparent: false, liquid: false, foliage: false, lightEmission: 0 },
      collision: { minX: 0.0625, minY: 0, minZ: 0.0625, maxX: 0.9375, maxY: 1, maxZ: 0.9375 },
    });
    opaque(41, "gold_block", { top: Tex.GOLD_BLOCK, bottom: Tex.GOLD_BLOCK, side: Tex.GOLD_BLOCK });
    opaque(42, "iron_block", { top: Tex.IRON_BLOCK, bottom: Tex.IRON_BLOCK, side: Tex.IRON_BLOCK });
    opaque(45, "brick", { top: Tex.BRICK, bottom: Tex.BRICK, side: Tex.BRICK });
    opaque(48, "mossy_cobblestone", { top: Tex.MOSSY_COBBLESTONE, bottom: Tex.MOSSY_COBBLESTONE, side: Tex.MOSSY_COBBLESTONE });
    opaque(49, "obsidian", { top: Tex.OBSIDIAN, bottom: Tex.OBSIDIAN, side: Tex.OBSIDIAN });
    opaque(54, "chest", { top: Tex.PLANKS_OAK, bottom: Tex.PLANKS_OAK, side: Tex.PLANKS_OAK });
    opaque(56, "diamond_ore", { top: Tex.DIAMOND_ORE, bottom: Tex.DIAMOND_ORE, side: Tex.DIAMOND_ORE });
    opaque(57, "diamond_block", { top: Tex.DIAMOND_BLOCK, bottom: Tex.DIAMOND_BLOCK, side: Tex.DIAMOND_BLOCK });
    opaque(58, "crafting_table", { top: Tex.CRAFTING_TABLE_TOP, bottom: Tex.PLANKS_OAK, side: Tex.CRAFTING_TABLE_SIDE });
    emitter(62, "furnace_active", { top: Tex.FURNACE_TOP, bottom: Tex.FURNACE_TOP, side: Tex.FURNACE_SIDE }, 13);
    opaque(73, "redstone_ore", { top: Tex.REDSTONE_ORE, bottom: Tex.REDSTONE_ORE, side: Tex.REDSTONE_ORE });
    emitter(89, "glowstone", { top: Tex.GLOWSTONE, bottom: Tex.GLOWSTONE, side: Tex.GLOWSTONE }, 15);
    registry.register({
      id: 55,
      name: "redstone_wire",
      textures: { top: Tex.REDSTONE_WIRE_0, bottom: Tex.REDSTONE_WIRE_0, side: Tex.REDSTONE_WIRE_0 },
      material: { opaque: false, transparent: true, liquid: false, foliage: false, lightEmission: 0 },
      collision: EMPTY_AABB,
    });
    registry.register({
      id: 75,
      name: "redstone_torch_off",
      textures: { top: Tex.REDSTONE_WIRE_0, bottom: Tex.REDSTONE_WIRE_0, side: Tex.REDSTONE_WIRE_0 },
      material: { opaque: false, transparent: true, liquid: false, foliage: false, lightEmission: 0 },
      collision: EMPTY_AABB,
    });
    registry.register({
      id: 76,
      name: "redstone_torch_on",
      textures: { top: Tex.REDSTONE_WIRE_15, bottom: Tex.REDSTONE_WIRE_15, side: Tex.REDSTONE_WIRE_15 },
      material: { opaque: false, transparent: true, liquid: false, foliage: false, lightEmission: 14 },
      collision: EMPTY_AABB,
    });
    registry.register({
      id: 93,
      name: "repeater_active",
      textures: { top: Tex.REDSTONE_WIRE_10, bottom: Tex.REDSTONE_WIRE_10, side: Tex.REDSTONE_WIRE_10 },
      material: { opaque: false, transparent: true, liquid: false, foliage: false, lightEmission: 0 },
      collision: EMPTY_AABB,
    });
    registry.register({
      id: 123,
      name: "redstone_lamp",
      textures: { top: Tex.REDSTONE_LAMP_OFF, bottom: Tex.REDSTONE_LAMP_OFF, side: Tex.REDSTONE_LAMP_OFF },
      material: { opaque: true, transparent: false, liquid: false, foliage: false, lightEmission: 0 },
      collision: FULL_BLOCK_AABB,
    });
  }
}
