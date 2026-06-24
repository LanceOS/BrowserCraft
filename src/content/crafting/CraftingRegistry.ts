export interface CraftingOutput {
  readonly id: number;
  readonly count: number;
  readonly metadata?: number;
}

export interface ShapedRecipe {
  readonly id: string;
  readonly pattern: readonly string[];
  readonly key: Readonly<Record<string, number>>;
  readonly output: CraftingOutput;
  readonly gridWidth: number;
  readonly gridHeight: number;
}

export interface ShapelessRecipe {
  readonly id: string;
  readonly ingredients: readonly number[];
  readonly output: CraftingOutput;
}

export class CraftingRegistry {
  private readonly shaped: ShapedRecipe[] = [];
  private readonly shapeless: ShapelessRecipe[] = [];

  registerShaped(recipe: ShapedRecipe): void {
    this.shaped.push(recipe);
  }

  registerShapeless(recipe: ShapelessRecipe): void {
    this.shapeless.push(recipe);
  }

  getShaped(): readonly ShapedRecipe[] {
    return this.shaped;
  }

  getShapeless(): readonly ShapelessRecipe[] {
    return this.shapeless;
  }
}

export const createDefaultCraftingRegistry = (): CraftingRegistry => {
  const registry = new CraftingRegistry();
  registry.registerShapeless({
    id: "planks_from_log",
    ingredients: [17],
    output: { id: 5, count: 4 },
  });
  registry.registerShaped({
    id: "sticks",
    pattern: ["X", "X"],
    key: { X: 5 },
    output: { id: 280, count: 4 },
    gridWidth: 1,
    gridHeight: 2,
  });
  registry.registerShaped({
    id: "crafting_table",
    pattern: ["XX", "XX"],
    key: { X: 5 },
    output: { id: 58, count: 1 },
    gridWidth: 2,
    gridHeight: 2,
  });
  return registry;
};
