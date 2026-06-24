import type { ComponentStore } from "./ComponentStore.js";

export const rowsWithAll = (...stores: Array<ComponentStore<any>>): number[] => {
  if (stores.length === 0) return [];

  let smallest = stores[0];
  for (const store of stores) {
    if (store.count < smallest.count) smallest = store;
  }

  const result: number[] = [];
  for (const row of smallest.rows()) {
    const entityIndex = smallest.entityAtRow(row);
    let found = true;
    for (const store of stores) {
      if (store === smallest) continue;
      if (store.rowFor(entityIndex) === -1) {
        found = false;
        break;
      }
    }
    if (found) result.push(row);
  }

  return result;
};
