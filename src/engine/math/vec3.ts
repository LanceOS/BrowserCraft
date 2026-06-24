export type Vec3Tuple = [number, number, number];

export const createVec3 = (x = 0, y = 0, z = 0): Float32Array => new Float32Array([x, y, z]);

export const setVec3 = (out: Float32Array, x: number, y: number, z: number): Float32Array => {
  out[0] = x;
  out[1] = y;
  out[2] = z;
  return out;
};

export const copyVec3 = (out: Float32Array, src: ArrayLike<number>): Float32Array => {
  out[0] = src[0];
  out[1] = src[1];
  out[2] = src[2];
  return out;
};

export const addVec3 = (out: Float32Array, a: ArrayLike<number>, b: ArrayLike<number>): Float32Array => {
  out[0] = a[0] + b[0];
  out[1] = a[1] + b[1];
  out[2] = a[2] + b[2];
  return out;
};

export const subVec3 = (out: Float32Array, a: ArrayLike<number>, b: ArrayLike<number>): Float32Array => {
  out[0] = a[0] - b[0];
  out[1] = a[1] - b[1];
  out[2] = a[2] - b[2];
  return out;
};

export const scaleVec3 = (out: Float32Array, a: ArrayLike<number>, scale: number): Float32Array => {
  out[0] = a[0] * scale;
  out[1] = a[1] * scale;
  out[2] = a[2] * scale;
  return out;
};

export const crossVec3 = (out: Float32Array, a: ArrayLike<number>, b: ArrayLike<number>): Float32Array => {
  const ax = a[0];
  const ay = a[1];
  const az = a[2];
  const bx = b[0];
  const by = b[1];
  const bz = b[2];

  out[0] = ay * bz - az * by;
  out[1] = az * bx - ax * bz;
  out[2] = ax * by - ay * bx;
  return out;
};

export const normalizeVec3 = (out: Float32Array, src: ArrayLike<number>): Float32Array => {
  const x = src[0];
  const y = src[1];
  const z = src[2];
  const len = Math.hypot(x, y, z) || 1;
  out[0] = x / len;
  out[1] = y / len;
  out[2] = z / len;
  return out;
};

export const dotVec3 = (a: ArrayLike<number>, b: ArrayLike<number>): number =>
  a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
