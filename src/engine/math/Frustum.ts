export class Frustum {
  readonly planes = new Float32Array(24);

  extractFrom(viewProjection: ArrayLike<number>): void {
    const p = this.planes;

    p[0] = viewProjection[3] + viewProjection[0];
    p[1] = viewProjection[7] + viewProjection[4];
    p[2] = viewProjection[11] + viewProjection[8];
    p[3] = viewProjection[15] + viewProjection[12];

    p[4] = viewProjection[3] - viewProjection[0];
    p[5] = viewProjection[7] - viewProjection[4];
    p[6] = viewProjection[11] - viewProjection[8];
    p[7] = viewProjection[15] - viewProjection[12];

    p[8] = viewProjection[3] + viewProjection[1];
    p[9] = viewProjection[7] + viewProjection[5];
    p[10] = viewProjection[11] + viewProjection[9];
    p[11] = viewProjection[15] + viewProjection[13];

    p[12] = viewProjection[3] - viewProjection[1];
    p[13] = viewProjection[7] - viewProjection[5];
    p[14] = viewProjection[11] - viewProjection[9];
    p[15] = viewProjection[15] - viewProjection[13];

    p[16] = viewProjection[3] + viewProjection[2];
    p[17] = viewProjection[7] + viewProjection[6];
    p[18] = viewProjection[11] + viewProjection[10];
    p[19] = viewProjection[15] + viewProjection[14];

    p[20] = viewProjection[3] - viewProjection[2];
    p[21] = viewProjection[7] - viewProjection[6];
    p[22] = viewProjection[11] - viewProjection[10];
    p[23] = viewProjection[15] - viewProjection[14];

    for (let i = 0; i < 6; i++) {
      const base = i * 4;
      const length = Math.hypot(p[base], p[base + 1], p[base + 2]) || 1;
      p[base] /= length;
      p[base + 1] /= length;
      p[base + 2] /= length;
      p[base + 3] /= length;
    }
  }
}
