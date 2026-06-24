import { createMat4, invertMat4, lookAtMat4, multiplyMat4, perspectiveMat4 } from "../math/mat4.js";
import { createVec3, normalizeVec3, crossVec3 } from "../math/vec3.js";

const WORLD_UP = new Float32Array([0, 1, 0]);

export class Camera {
  readonly position = createVec3(0, 82, 0);
  readonly forward = createVec3(0, -0.2, -1);
  readonly right = createVec3(1, 0, 0);
  readonly up = createVec3(0, 1, 0);
  readonly projectionMatrix = createMat4();
  readonly viewMatrix = createMat4();
  readonly viewProjectionMatrix = createMat4();
  readonly inverseViewProjectionMatrix = createMat4();
  yaw = -Math.PI / 2;
  pitch = -0.35;

  resize(aspect: number, fovDegrees: number): void {
    perspectiveMat4(this.projectionMatrix, (fovDegrees * Math.PI) / 180, aspect, 0.1, 600);
  }

  updateMatrices(): void {
    this.forward[0] = Math.cos(this.yaw) * Math.cos(this.pitch);
    this.forward[1] = Math.sin(this.pitch);
    this.forward[2] = Math.sin(this.yaw) * Math.cos(this.pitch);
    normalizeVec3(this.forward, this.forward);
    crossVec3(this.right, this.forward, WORLD_UP);
    normalizeVec3(this.right, this.right);
    crossVec3(this.up, this.right, this.forward);
    normalizeVec3(this.up, this.up);

    const target = new Float32Array([
      this.position[0] + this.forward[0],
      this.position[1] + this.forward[1],
      this.position[2] + this.forward[2],
    ]);

    lookAtMat4(this.viewMatrix, this.position, target, this.up);
    multiplyMat4(this.viewProjectionMatrix, this.projectionMatrix, this.viewMatrix);
    invertMat4(this.inverseViewProjectionMatrix, this.viewProjectionMatrix);
  }
}
