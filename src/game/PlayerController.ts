import type { Camera } from "../engine/render/Camera.js";
import { InputState } from "./InputState.js";

export class PlayerController {
  constructor(
    private readonly input: InputState,
    private readonly camera: Camera,
  ) {}

  update(dt: number): void {
    const [dx, dy] = this.input.consumeLook();
    this.camera.yaw += dx * 0.0022;
    this.camera.pitch = Math.max(-1.5, Math.min(1.5, this.camera.pitch - dy * 0.0022));
    this.camera.updateMatrices();

    const speed = this.input.isPressed("ShiftLeft") ? 18 : 10;
    const verticalSpeed = this.input.isPressed("ShiftRight") ? 20 : speed;
    const moveX =
      (this.input.isPressed("KeyD") ? 1 : 0) -
      (this.input.isPressed("KeyA") ? 1 : 0);
    const moveZ =
      (this.input.isPressed("KeyW") ? 1 : 0) -
      (this.input.isPressed("KeyS") ? 1 : 0);
    const moveY =
      (this.input.isPressed("Space") ? 1 : 0) -
      (this.input.isPressed("ControlLeft") ? 1 : 0);

    if (moveX !== 0 || moveZ !== 0) {
      const length = Math.hypot(moveX, moveZ) || 1;
      const forwardX = this.camera.forward[0];
      const forwardZ = this.camera.forward[2];
      const forwardLength = Math.hypot(forwardX, forwardZ) || 1;
      const flatForwardX = forwardX / forwardLength;
      const flatForwardZ = forwardZ / forwardLength;
      const rightX = this.camera.right[0];
      const rightZ = this.camera.right[2];

      this.camera.position[0] += ((flatForwardX * moveZ + rightX * moveX) / length) * speed * dt;
      this.camera.position[2] += ((flatForwardZ * moveZ + rightZ * moveX) / length) * speed * dt;
    }

    if (moveY !== 0) {
      this.camera.position[1] += moveY * verticalSpeed * dt;
    }
  }
}
