export const MAX_POWER = 15;

export function getPower(packed: number): number {
  return packed & 0x0f;
}

export function getState(packed: number): number {
  return (packed >> 4) & 0x0f;
}

export function packRedstone(power: number, state: number): number {
  return ((state & 0x0f) << 4) | (power & 0x0f);
}
