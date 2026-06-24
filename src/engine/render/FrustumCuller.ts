import { Frustum } from "../math/Frustum.js";

export class FrustumCuller {
  private readonly frustum = new Frustum();

  extractFrom(viewProjection: ArrayLike<number>): void {
    this.frustum.extractFrom(viewProjection);
  }

  testAABB(min: ArrayLike<number>, max: ArrayLike<number>): boolean {
    const planes = this.frustum.planes;
    for (let i = 0; i < 6; i++) {
      const base = i * 4;
      const nx = planes[base + 0];
      const ny = planes[base + 1];
      const nz = planes[base + 2];
      const d = planes[base + 3];
      const px = nx > 0 ? max[0] : min[0];
      const py = ny > 0 ? max[1] : min[1];
      const pz = nz > 0 ? max[2] : min[2];
      if (nx * px + ny * py + nz * pz + d < 0) return false;
    }
    return true;
  }
}
