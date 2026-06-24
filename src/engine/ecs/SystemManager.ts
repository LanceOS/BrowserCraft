export type SystemStage = "prePhysics" | "physics" | "postPhysics" | "render";

export interface System<TState> {
  readonly name: string;
  readonly stage: SystemStage;
  update(state: TState, dt: number): void;
}

export class SystemManager<TState> {
  private readonly systems: Array<System<TState>> = [];

  add(system: System<TState>): void {
    this.systems.push(system);
    this.sort();
  }

  update(state: TState, dt: number): void {
    for (const system of this.systems) system.update(state, dt);
  }

  private sort(): void {
    const order: Record<SystemStage, number> = {
      prePhysics: 0,
      physics: 1,
      postPhysics: 2,
      render: 3,
    };
    this.systems.sort((a, b) => order[a.stage] - order[b.stage]);
  }
}
