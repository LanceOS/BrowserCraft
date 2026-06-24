import type { ComponentStore } from "../ComponentStore.js";
import { PlayerComponentDesc } from "../components/PlayerComponent.js";
import { TransformDesc } from "../components/Transform.js";
import { RigidBodyDesc } from "../components/RigidBody.js";
import type { System } from "../SystemManager.js";
import { InputState } from "../../core/InputState.js";

export class PlayerControllerSystem<TState> implements System<TState> {
  readonly name = "playerController";
  readonly stage = "prePhysics" as const;

  constructor(
    private readonly transforms: ComponentStore<typeof TransformDesc>,
    private readonly bodies: ComponentStore<typeof RigidBodyDesc>,
    private readonly players: ComponentStore<typeof PlayerComponentDesc>,
    private readonly input: InputState,
  ) {}

  update(_state: TState, _dt: number): void {
    const yawData = this.players.data.yaw;
    const pitchData = this.players.data.pitch;

    for (const playerRow of this.players.rows()) {
      const entityIndex = this.players.entityAtRow(playerRow);
      const bodyRow = this.bodies.rowFor(entityIndex);
      const transformRow = this.transforms.rowFor(entityIndex);
      if (bodyRow === -1 || transformRow === -1) continue;

      if (this.input.pointerLocked) {
        const sensitivity = 0.0025;
        yawData[playerRow] -= this.input.mouseDelta[0] * sensitivity;
        pitchData[playerRow] -= this.input.mouseDelta[1] * sensitivity;
        const maxPitch = Math.PI * 0.5 - 0.01;
        if (pitchData[playerRow] > maxPitch) pitchData[playerRow] = maxPitch;
        if (pitchData[playerRow] < -maxPitch) pitchData[playerRow] = -maxPitch;
      }

      const yaw = yawData[playerRow];
      const forwardX = -Math.sin(yaw);
      const forwardZ = -Math.cos(yaw);
      const rightX = Math.cos(yaw);
      const rightZ = -Math.sin(yaw);

      let moveX = 0;
      let moveZ = 0;
      if (this.input.pointerLocked) {
        if (this.input.isHeld(0)) { moveX += forwardX; moveZ += forwardZ; }
        if (this.input.isHeld(2)) { moveX -= forwardX; moveZ -= forwardZ; }
        if (this.input.isHeld(1)) { moveX -= rightX; moveZ -= rightZ; }
        if (this.input.isHeld(3)) { moveX += rightX; moveZ += rightZ; }
      }

      const lenSq = moveX * moveX + moveZ * moveZ;
      if (lenSq > 0) {
        const invLen = 1 / Math.sqrt(lenSq);
        moveX *= invLen;
        moveZ *= invLen;
      }

      const flying = this.players.data.isFlying[playerRow] === 1;
      const sprinting = this.input.isHeld(5) && !flying;
      const speed = flying
        ? this.players.data.flySpeed[playerRow]
        : sprinting
          ? this.players.data.sprintSpeed[playerRow]
          : this.players.data.walkSpeed[playerRow];

      const velocity = this.bodies.data.velocity;
      const gravity = this.bodies.data.gravity;
      velocity[bodyRow * 3 + 0] = moveX * speed;
      velocity[bodyRow * 3 + 2] = moveZ * speed;

      if (flying) {
        gravity[bodyRow] = 0;
        velocity[bodyRow * 3 + 1] = 0;
        if (this.input.pointerLocked && this.input.isHeld(4)) velocity[bodyRow * 3 + 1] = speed;
        if (this.input.pointerLocked && this.input.isHeld(5)) velocity[bodyRow * 3 + 1] = -speed;
      } else {
        gravity[bodyRow] = 20;
        if (this.input.pointerLocked && this.input.isPressed(4) && this.bodies.data.onGround[bodyRow] === 1) {
          velocity[bodyRow * 3 + 1] = 8;
          this.bodies.data.onGround[bodyRow] = 0;
        }
      }

      void transformRow;
    }
  }
}
