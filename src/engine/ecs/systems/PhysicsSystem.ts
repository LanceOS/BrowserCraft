import { EntityManager } from "../EntityManager.js";
import type { ComponentStore } from "../ComponentStore.js";
import { TransformDesc } from "../components/Transform.js";
import { RigidBodyDesc } from "../components/RigidBody.js";
import type { System } from "../SystemManager.js";
import type { World } from "../../../world/World.js";

export class PhysicsSystem<TState> implements System<TState> {
  readonly name = "physics";
  readonly stage = "physics" as const;
  private static readonly TERMINAL_VELOCITY = -55;
  private static readonly STEP_HEIGHT = 0.52;

  constructor(
    private readonly entities: EntityManager,
    private readonly transforms: ComponentStore<typeof TransformDesc>,
    private readonly bodies: ComponentStore<typeof RigidBodyDesc>,
    private readonly world: World,
  ) {}

  update(_state: TState, dt: number): void {
    const positions = this.transforms.data.position;
    const velocities = this.bodies.data.velocity;
    const mins = this.bodies.data.aabbMin;
    const maxs = this.bodies.data.aabbMax;
    const grounds = this.bodies.data.onGround;
    const fluids = this.bodies.data.isFluid;
    const gravity = this.bodies.data.gravity;
    const drag = this.bodies.data.drag;

    for (const bodyRow of this.bodies.rows()) {
      const entityIndex = this.bodies.entityAtRow(bodyRow);
      const transformRow = this.transforms.rowFor(entityIndex);
      if (transformRow === -1) continue;

      const bodyBase = bodyRow * 3;
      const transformBase = transformRow * 3;
      const fluid = this.overlapsFluid(transformBase, bodyBase, positions, mins, maxs);
      fluids[bodyRow] = fluid ? 1 : 0;

      const gravityValue = fluid ? Math.min(gravity[bodyRow], 4) : gravity[bodyRow];
      const dragValue = fluid ? Math.min(drag[bodyRow], 0.8) : drag[bodyRow];

      velocities[bodyBase + 1] -= gravityValue * dt;
      if (fluid) velocities[bodyBase + 1] *= 0.92;
      if (velocities[bodyBase + 1] < PhysicsSystem.TERMINAL_VELOCITY) {
        velocities[bodyBase + 1] = PhysicsSystem.TERMINAL_VELOCITY;
      }
      velocities[bodyBase + 0] *= dragValue;
      velocities[bodyBase + 2] *= dragValue;

      const dx = velocities[bodyBase + 0] * dt;
      const dy = velocities[bodyBase + 1] * dt;
      const dz = velocities[bodyBase + 2] * dt;
      const maxDelta = Math.max(Math.abs(dx), Math.abs(dy), Math.abs(dz));
      const steps = Math.max(1, Math.ceil(maxDelta / 0.5));
      const invSteps = 1 / steps;

      for (let stepIndex = 0; stepIndex < steps; stepIndex++) {
        const stepDy = dy * invSteps;
        const stepDx = dx * invSteps;
        const stepDz = dz * invSteps;

        if (stepDy !== 0) {
          positions[transformBase + 1] += stepDy;
          if (this.collides(transformBase, bodyBase, positions, mins, maxs)) {
            positions[transformBase + 1] -= stepDy;
            if (stepDy < 0) {
              positions[transformBase + 1] =
                Math.floor(positions[transformBase + 1] + mins[bodyBase + 1]) - mins[bodyBase + 1];
              grounds[bodyRow] = 1;
            } else {
              positions[transformBase + 1] =
                Math.ceil(positions[transformBase + 1] + maxs[bodyBase + 1]) - 1 - maxs[bodyBase + 1];
            }
            velocities[bodyBase + 1] = 0;
          } else if (stepDy < 0) {
            grounds[bodyRow] = 0;
          }
        }

        if (stepDx !== 0) {
          positions[transformBase + 0] += stepDx;
          if (this.collides(transformBase, bodyBase, positions, mins, maxs)) {
            if (grounds[bodyRow] === 1 && this.tryStepUp(transformBase, bodyBase, positions, mins, maxs)) {
              grounds[bodyRow] = 0;
            } else {
              positions[transformBase + 0] -= stepDx;
              if (stepDx > 0) {
                positions[transformBase + 0] =
                  Math.floor(positions[transformBase + 0] + maxs[bodyBase + 0]) - maxs[bodyBase + 0];
              } else {
                positions[transformBase + 0] =
                  Math.ceil(positions[transformBase + 0] + mins[bodyBase + 0]) - 1 - mins[bodyBase + 0];
              }
              velocities[bodyBase + 0] = 0;
            }
          }
        }

        if (stepDz !== 0) {
          positions[transformBase + 2] += stepDz;
          if (this.collides(transformBase, bodyBase, positions, mins, maxs)) {
            if (grounds[bodyRow] === 1 && this.tryStepUp(transformBase, bodyBase, positions, mins, maxs)) {
              grounds[bodyRow] = 0;
            } else {
              positions[transformBase + 2] -= stepDz;
              if (stepDz > 0) {
                positions[transformBase + 2] =
                  Math.floor(positions[transformBase + 2] + maxs[bodyBase + 2]) - maxs[bodyBase + 2];
              } else {
                positions[transformBase + 2] =
                  Math.ceil(positions[transformBase + 2] + mins[bodyBase + 2]) - 1 - mins[bodyBase + 2];
              }
              velocities[bodyBase + 2] = 0;
            }
          }
        }
      }
    }

    void this.entities;
  }

  private collides(
    transformBase: number,
    bodyBase: number,
    positions: Float32Array,
    mins: Float32Array,
    maxs: Float32Array,
  ): boolean {
    const minX = Math.floor(positions[transformBase + 0] + mins[bodyBase + 0]);
    const maxX = Math.floor(positions[transformBase + 0] + maxs[bodyBase + 0] - 1e-6);
    const minY = Math.floor(positions[transformBase + 1] + mins[bodyBase + 1]);
    const maxY = Math.floor(positions[transformBase + 1] + maxs[bodyBase + 1] - 1e-6);
    const minZ = Math.floor(positions[transformBase + 2] + mins[bodyBase + 2]);
    const maxZ = Math.floor(positions[transformBase + 2] + maxs[bodyBase + 2] - 1e-6);

    for (let y = minY; y <= maxY; y++) {
      for (let z = minZ; z <= maxZ; z++) {
        for (let x = minX; x <= maxX; x++) {
          if (this.world.isSolid(x, y, z)) return true;
        }
      }
    }

    return false;
  }

  private overlapsFluid(
    transformBase: number,
    bodyBase: number,
    positions: Float32Array,
    mins: Float32Array,
    maxs: Float32Array,
  ): boolean {
    const minX = Math.floor(positions[transformBase + 0] + mins[bodyBase + 0]);
    const maxX = Math.floor(positions[transformBase + 0] + maxs[bodyBase + 0] - 1e-6);
    const minY = Math.floor(positions[transformBase + 1] + mins[bodyBase + 1]);
    const maxY = Math.floor(positions[transformBase + 1] + maxs[bodyBase + 1] - 1e-6);
    const minZ = Math.floor(positions[transformBase + 2] + mins[bodyBase + 2]);
    const maxZ = Math.floor(positions[transformBase + 2] + maxs[bodyBase + 2] - 1e-6);

    for (let y = minY; y <= maxY; y++) {
      for (let z = minZ; z <= maxZ; z++) {
        for (let x = minX; x <= maxX; x++) {
          if (this.world.isFluid(x, y, z)) return true;
        }
      }
    }

    return false;
  }

  private tryStepUp(
    transformBase: number,
    bodyBase: number,
    positions: Float32Array,
    mins: Float32Array,
    maxs: Float32Array,
  ): boolean {
    positions[transformBase + 1] += PhysicsSystem.STEP_HEIGHT;
    if (this.collides(transformBase, bodyBase, positions, mins, maxs)) {
      positions[transformBase + 1] -= PhysicsSystem.STEP_HEIGHT;
      return false;
    }
    return true;
  }
}
