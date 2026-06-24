import { SimplexNoise } from "./SimplexNoise.js";

export class CaveCarver {
  private readonly noise: SimplexNoise;
  private readonly rng: () => number;

  constructor(seed: number) {
    this.noise = new SimplexNoise(seed ^ 0xcafe);
    let s = seed ^ 0xc4e5;
    this.rng = () => {
      s = (Math.imul(s, 1664525) + 1013904223) | 0;
      return (s >>> 0) / 4294967296;
    };
  }

  carve(voxels: Uint8Array, baseX: number, baseZ: number, sizeX: number, sizeY: number, sizeZ: number): void {
    const numWorms = 4;

    for (let worm = 0; worm < numWorms; worm++) {
      let x = this.rng() * sizeX;
      let y = 10 + this.rng() * 40;
      let z = this.rng() * sizeZ;
      let yaw = this.rng() * Math.PI * 2;
      let pitch = (this.rng() - 0.5) * Math.PI * 0.5;
      const length = 40 + Math.floor(this.rng() * 80);

      for (let step = 0; step < length; step++) {
        const worldX = baseX + x;
        const worldY = y;
        const worldZ = baseZ + z;

        yaw += this.noise.noise3D(worldX * 0.05, worldY * 0.05, worldZ * 0.05) * 0.2;
        pitch += this.noise.noise3D(worldX * 0.05 + 10, worldY * 0.05, worldZ * 0.05) * 0.1;

        x += Math.cos(pitch) * Math.cos(yaw);
        y += Math.sin(pitch);
        z += Math.cos(pitch) * Math.sin(yaw);

        const radius = 1.5 + this.noise.noise3D(worldX * 0.1, worldY * 0.1, worldZ * 0.1) * 0.5;
        this.carveSphere(voxels, x, y, z, radius, sizeX, sizeY, sizeZ);
      }
    }
  }

  private carveSphere(
    voxels: Uint8Array,
    cx: number,
    cy: number,
    cz: number,
    radius: number,
    sizeX: number,
    sizeY: number,
    sizeZ: number,
  ): void {
    const minX = Math.max(0, Math.floor(cx - radius));
    const maxX = Math.min(sizeX - 1, Math.ceil(cx + radius));
    const minY = Math.max(0, Math.floor(cy - radius));
    const maxY = Math.min(sizeY - 1, Math.ceil(cy + radius));
    const minZ = Math.max(0, Math.floor(cz - radius));
    const maxZ = Math.min(sizeZ - 1, Math.ceil(cz + radius));
    const radiusSq = radius * radius;

    for (let y = minY; y <= maxY; y++) {
      for (let z = minZ; z <= maxZ; z++) {
        for (let x = minX; x <= maxX; x++) {
          const dx = x - cx;
          const dy = y - cy;
          const dz = z - cz;
          if (dx * dx + dy * dy + dz * dz > radiusSq) continue;
          const index = (y * sizeZ + z) * sizeX + x;
          if (voxels[index] !== 0 && voxels[index] !== 8 && voxels[index] !== 10) {
            voxels[index] = 0;
          }
        }
      }
    }
  }
}
