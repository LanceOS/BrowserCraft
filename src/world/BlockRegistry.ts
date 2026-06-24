import type { BlockDefinition } from "./blocks/BlockDefinition.js";

export class BlockRegistry {
  private readonly defs: Array<BlockDefinition | null>;
  private readonly byName = new Map<string, number>();

  constructor(readonly capacity: number = 4096) {
    this.defs = new Array(capacity).fill(null);
  }

  register(def: BlockDefinition): void {
    if (this.defs[def.id]) {
      throw new Error(`Block id ${def.id} already registered`);
    }
    this.defs[def.id] = def;
    this.byName.set(def.name, def.id);
  }

  get(id: number): BlockDefinition {
    const def = this.defs[id];
    if (!def) throw new Error(`Unknown block id ${id}`);
    return def;
  }

  tryGet(id: number): BlockDefinition | null {
    return this.defs[id];
  }

  byNameGet(name: string): number | undefined {
    return this.byName.get(name);
  }

  values(): BlockDefinition[] {
    return this.defs.filter((def): def is BlockDefinition => def !== null);
  }
}
