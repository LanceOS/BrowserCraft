const AO_LUT = new Uint8Array(8);

for (let i = 0; i < 8; i++) {
  const s1 = (i >> 2) & 1;
  const s2 = (i >> 1) & 1;
  const c = i & 1;
  AO_LUT[i] = s1 && s2 ? 0 : 3 - (s1 + s2 + c);
}

export const calculateAO = (s1: number, s2: number, c: number): number => {
  const index = ((s1 & 1) << 2) | ((s2 & 1) << 1) | (c & 1);
  return AO_LUT[index];
};
