import type { System } from "../SystemManager.js";

export class PathfindingSystem<TState> implements System<TState> {
  readonly name = "pathfinding";
  readonly stage = "postPhysics" as const;

  update(_state: TState, _dt: number): void {
    // Architecture placeholder: path requests will live here.
  }
}
