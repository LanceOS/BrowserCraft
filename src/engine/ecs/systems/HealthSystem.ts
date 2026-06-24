import type { ComponentStore } from "../ComponentStore.js";
import { HealthDesc } from "../components/Health.js";
import type { System } from "../SystemManager.js";

export class HealthSystem<TState> implements System<TState> {
  readonly name = "health";
  readonly stage = "postPhysics" as const;

  constructor(private readonly healths?: ComponentStore<typeof HealthDesc>) {}

  update(_state: TState, dt: number): void {
    if (!this.healths) return;

    const hp = this.healths.data.hp;
    const maxHp = this.healths.data.maxHp;
    const regenCd = this.healths.data.regenCd;

    for (const row of this.healths.rows()) {
      if (hp[row] <= 0) continue;
      if (regenCd[row] > 0) {
        regenCd[row] = Math.max(0, regenCd[row] - dt);
        continue;
      }

      if (hp[row] < maxHp[row]) {
        hp[row] += 1;
        regenCd[row] = 5;
      }
    }
  }
}
