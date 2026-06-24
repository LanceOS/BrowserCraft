import type { BlockMaterial } from "./BlockMaterial.js";
import type { BlockAABB } from "./AABB.js";

export interface BlockTextures {
  readonly top: number;
  readonly bottom: number;
  readonly side: number;
}

export interface BlockDefinition {
  readonly id: number;
  readonly name: string;
  readonly textures: BlockTextures;
  readonly material: BlockMaterial;
  readonly collision: BlockAABB;
}
