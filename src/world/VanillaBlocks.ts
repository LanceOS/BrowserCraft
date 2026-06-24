import type { BlockDefinition } from "./blocks/BlockDefinition.js";
import { FULL_BLOCK_AABB } from "./blocks/AABB.js";

export const VANILLA_BLOCKS: BlockDefinition[] = [
  {
    id: 1,
    name: "stone",
    textures: { top: 1, bottom: 1, side: 1 },
    material: { opaque: true, transparent: false, liquid: false, foliage: false, lightEmission: 0 },
    collision: FULL_BLOCK_AABB,
  },
  {
    id: 2,
    name: "grass",
    textures: { top: 2, bottom: 3, side: 4 },
    material: { opaque: true, transparent: false, liquid: false, foliage: false, lightEmission: 0 },
    collision: FULL_BLOCK_AABB,
  },
  {
    id: 3,
    name: "dirt",
    textures: { top: 3, bottom: 3, side: 3 },
    material: { opaque: true, transparent: false, liquid: false, foliage: false, lightEmission: 0 },
    collision: FULL_BLOCK_AABB,
  },
  {
    id: 4,
    name: "sand",
    textures: { top: 5, bottom: 5, side: 5 },
    material: { opaque: true, transparent: false, liquid: false, foliage: false, lightEmission: 0 },
    collision: FULL_BLOCK_AABB,
  },
  {
    id: 5,
    name: "log",
    textures: { top: 6, bottom: 6, side: 7 },
    material: { opaque: true, transparent: false, liquid: false, foliage: false, lightEmission: 0 },
    collision: FULL_BLOCK_AABB,
  },
  {
    id: 6,
    name: "leaves",
    textures: { top: 8, bottom: 8, side: 8 },
    material: { opaque: false, transparent: true, liquid: false, foliage: true, lightEmission: 0 },
    collision: FULL_BLOCK_AABB,
  },
  {
    id: 7,
    name: "water",
    textures: { top: 9, bottom: 9, side: 9 },
    material: { opaque: false, transparent: true, liquid: true, foliage: false, lightEmission: 0 },
    collision: FULL_BLOCK_AABB,
  },
  {
    id: 8,
    name: "glass",
    textures: { top: 10, bottom: 10, side: 10 },
    material: { opaque: false, transparent: true, liquid: false, foliage: false, lightEmission: 0 },
    collision: FULL_BLOCK_AABB,
  },
];
