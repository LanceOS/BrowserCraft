import type { System } from "../SystemManager.js";

export class AISystem<TState> implements System<TState> {
  readonly name = "ai";
  readonly stage = "postPhysics" as const;

  update(_state: TState, _dt: number): void {
    // Architecture placeholder: behavior selection will live here.
  }
}
