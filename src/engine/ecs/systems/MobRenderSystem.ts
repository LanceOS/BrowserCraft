import type { System } from "../SystemManager.js";

export class MobRenderSystem<TState> implements System<TState> {
  readonly name = "mobRender";
  readonly stage = "render" as const;

  update(_state: TState, _dt: number): void {
    // Architecture placeholder: instanced mob submission will live here.
  }
}
