import { STRUCTURE_DEFINITIONS, type StructureDefinition } from "./StructureDefinitions.js";
import type { StructureBlueprint } from "./StructureBlueprint.js";

export class StructureFactory {
  private readonly blueprints = new Map<number, StructureBlueprint>();

  constructor(definitions: readonly StructureDefinition[] = STRUCTURE_DEFINITIONS) {
    for (const definition of definitions) {
      this.blueprints.set(definition.id, this.createBlueprint(definition));
    }
  }

  get(id: number): StructureBlueprint | undefined {
    return this.blueprints.get(id);
  }

  getAll(): StructureBlueprint[] {
    return Array.from(this.blueprints.values());
  }

  private createBlueprint(definition: StructureDefinition): StructureBlueprint {
    if (definition.blocks.length % 4 !== 0) {
      throw new Error(`Structure ${definition.id} block data must be x/y/z/id tuples`);
    }

    const { blocks: rawBlocks } = definition;
    const packed = new Uint8Array(rawBlocks.length + 4);
    for (let i = 0; i < rawBlocks.length; i++) {
      const value = rawBlocks[i];
      const tupleOffset = i & 3;
      const min = tupleOffset === 3 ? 0 : -127;
      const max = tupleOffset === 3 ? 255 : 127;
      if (value < min || value > max) {
        throw new Error(`Structure ${definition.id} value ${value} is outside packed byte range`);
      }
      packed[i] = rawBlocks[i] & 0xff;
    }
    packed[rawBlocks.length] = 0x80;
    packed[rawBlocks.length + 1] = 0;
    packed[rawBlocks.length + 2] = 0;
    packed[rawBlocks.length + 3] = 0;

    return {
      id: definition.id,
      sizeX: definition.sizeX,
      sizeY: definition.sizeY,
      sizeZ: definition.sizeZ,
      blocks: packed,
      paletteWeight: definition.paletteWeight ?? 1,
    };
  }
}
