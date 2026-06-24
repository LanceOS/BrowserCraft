import type { ComponentStore } from "../ComponentStore.js";
import { PlayerComponentDesc } from "../components/PlayerComponent.js";
import { TransformDesc } from "../components/Transform.js";
import { createMat4, invertMat4, lookAtMat4, multiplyMat4, perspectiveMat4 } from "../../math/mat4.js";
import { createVec3, crossVec3, normalizeVec3 } from "../../math/vec3.js";
import type { CameraView } from "../../render/CameraView.js";

const WORLD_UP = new Float32Array([0, 1, 0]);

export class CameraSystem implements CameraView {
  readonly position = createVec3();
  readonly forward = createVec3(0, 0, -1);
  readonly right = createVec3(1, 0, 0);
  readonly up = createVec3(0, 1, 0);
  readonly projectionMatrix = createMat4();
  readonly viewMatrix = createMat4();
  readonly viewProjectionMatrix = createMat4();
  readonly inverseViewProjectionMatrix = createMat4();
  yaw = 0;
  pitch = 0;
  private aspectRatio = 16 / 9;
  private fovDegrees = 70;
  private readonly target = createVec3();

  constructor(
    private readonly transforms: ComponentStore<typeof TransformDesc>,
    private readonly players: ComponentStore<typeof PlayerComponentDesc>,
  ) {
    perspectiveMat4(this.projectionMatrix, (this.fovDegrees * Math.PI) / 180, this.aspectRatio, 0.1, 1000);
  }

  updateProjection(aspectRatio: number, fovDegrees: number): void {
    if (this.aspectRatio === aspectRatio && this.fovDegrees === fovDegrees) return;
    this.aspectRatio = aspectRatio;
    this.fovDegrees = fovDegrees;
    perspectiveMat4(this.projectionMatrix, (fovDegrees * Math.PI) / 180, aspectRatio, 0.1, 1000);
  }

  syncFromPlayer(): void {
    const playerRow = this.players.count > 0 ? this.players.rows().next().value : undefined;
    if (playerRow === undefined) return;
    const entityIndex = this.players.entityAtRow(playerRow);
    const transformRow = this.transforms.rowFor(entityIndex);
    if (transformRow === -1) return;

    const pos = this.transforms.data.position;
    const eyeHeight = this.players.data.eyeHeight[playerRow];
    this.yaw = this.players.data.yaw[playerRow];
    this.pitch = this.players.data.pitch[playerRow];

    this.position[0] = pos[transformRow * 3 + 0];
    this.position[1] = pos[transformRow * 3 + 1] + eyeHeight;
    this.position[2] = pos[transformRow * 3 + 2];

    const cosYaw = Math.cos(this.yaw);
    const sinYaw = Math.sin(this.yaw);
    const cosPitch = Math.cos(this.pitch);
    const sinPitch = Math.sin(this.pitch);

    this.forward[0] = -sinYaw * cosPitch;
    this.forward[1] = sinPitch;
    this.forward[2] = -cosYaw * cosPitch;
    normalizeVec3(this.forward, this.forward);
    crossVec3(this.right, this.forward, WORLD_UP);
    normalizeVec3(this.right, this.right);
    crossVec3(this.up, this.right, this.forward);
    normalizeVec3(this.up, this.up);
  }

  updateMatrices(): void {
    this.target[0] = this.position[0] + this.forward[0];
    this.target[1] = this.position[1] + this.forward[1];
    this.target[2] = this.position[2] + this.forward[2];
    lookAtMat4(this.viewMatrix, this.position, this.target, this.up);
    multiplyMat4(this.viewProjectionMatrix, this.projectionMatrix, this.viewMatrix);
    invertMat4(this.inverseViewProjectionMatrix, this.viewProjectionMatrix);
  }
}
