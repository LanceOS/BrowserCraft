import type { StructureBlueprint } from "./StructureBlueprint.js";

export class StructureFactory {
  private readonly blueprints = new Map<number, StructureBlueprint>();

  constructor() {
    this.registerWell();
    this.registerHouse();
    this.registerRoad();
  }

  get(id: number): StructureBlueprint | undefined {
    return this.blueprints.get(id);
  }

  getAll(): StructureBlueprint[] {
    return Array.from(this.blueprints.values());
  }

  private registerWell(): void {
    const blocks: number[] = [];
    for (let y = 0; y < 4; y++) {
      blocks.push(0, y, 0, 4, 2, y, 0, 4, 0, y, 2, 4, 2, y, 2, 4);
    }
    blocks.push(0, 0, 1, 8, 2, 0, 1, 8, 1, 0, 0, 8, 1, 0, 2, 8, 1, 0, 1, 8);
    this.blueprints.set(0, this.createBlueprint(0, 3, 4, 3, blocks));
  }

  private registerHouse(): void {
    const blocks: number[] = [];
    const PLANKS = 5;
    const LOG = 17;
    const GLASS = 20;

    for (let x = 0; x < 5; x++) {
      for (let z = 0; z < 5; z++) {
        blocks.push(x, 0, z, PLANKS);
      }
    }

    for (let y = 1; y <= 4; y++) {
      for (let i = 0; i < 5; i++) {
        if (!(y === 1 && i === 2) && !(y === 2 && i === 2)) {
          blocks.push(i, y, 0, LOG);
        }
        blocks.push(i, y, 4, LOG);
        blocks.push(0, y, i, LOG);
        blocks.push(4, y, i, LOG);
      }
    }

    blocks.push(2, 3, 0, GLASS, 2, 3, 4, GLASS, 0, 3, 2, GLASS, 4, 3, 2, GLASS);
    for (let x = -1; x <= 5; x++) {
      for (let z = -1; z <= 5; z++) {
        blocks.push(x, 5, z, PLANKS);
      }
    }

    this.blueprints.set(1, this.createBlueprint(1, 7, 6, 7, blocks));
  }

  private registerRoad(): void {
    const blocks: number[] = [];
    for (let x = 0; x < 16; x++) {
      blocks.push(x, 0, 0, 13, x, 0, 1, 13, x, 0, 2, 13);
    }
    this.blueprints.set(2, this.createBlueprint(2, 16, 1, 3, blocks));
  }

  private createBlueprint(id: number, sizeX: number, sizeY: number, sizeZ: number, rawBlocks: number[]): StructureBlueprint {
    const packed = new Uint8Array(rawBlocks.length + 4);
    for (let i = 0; i < rawBlocks.length; i++) {
      packed[i] = rawBlocks[i] & 0xff;
    }
    packed[rawBlocks.length] = 0x80;
    packed[rawBlocks.length + 1] = 0;
    packed[rawBlocks.length + 2] = 0;
    packed[rawBlocks.length + 3] = 0;

    return {
      id,
      sizeX,
      sizeY,
      sizeZ,
      blocks: packed,
      paletteWeight: 1,
    };
  }
}
