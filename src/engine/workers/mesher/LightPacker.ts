export const MAX_LIGHT = 15;

export function getSkyLight(packed: number): number {
  return packed & 0x0f;
}

export function getBlockLight(packed: number): number {
  return (packed >> 4) & 0x0f;
}

export function packLight(sky: number, block: number): number {
  return ((block & 0x0f) << 4) | (sky & 0x0f);
}

export function packVertexLight(sky: number, block: number, ao: number): number {
  return ((ao & 0x03) << 16) | ((block & 0x0f) << 4) | (sky & 0x0f);
}
