import type { ChunkSlot } from "../../alloc/SharedPool.js";
import type { BlockRegistry } from "../../../world/BlockRegistry.js";
import { calculateAO } from "./AmbientOcclusion.js";
import { shouldCullFace } from "./FaceCuller.js";
import { MAX_LIGHT, getBlockLight, getSkyLight, packLight, packVertexLight } from "./LightPacker.js";
import { getPower } from "../redstone/RedstonePacker.js";

const BLOCK_ID_MASK = 0x0fff;
const VARIANT_SHIFT = 12;

const enum Axis {
  X = 0,
  Y = 1,
  Z = 2,
}

export interface NeighborChunkSlots {
  readonly negX?: ChunkSlot;
  readonly posX?: ChunkSlot;
  readonly negZ?: ChunkSlot;
  readonly posZ?: ChunkSlot;
}

export function greedyMeshChunk(
  slot: ChunkSlot,
  dims: { sizeX: number; sizeY: number; sizeZ: number; vertexStrideFloats: number },
  blocks: BlockRegistry,
  neighborSlots: NeighborChunkSlots = {},
): boolean {
  const { sizeX, sizeY, sizeZ, vertexStrideFloats } = dims;
  const voxels = slot.voxels;
  const light = slot.light;
  const redstone = slot.redstone;
  const vertices = slot.vertices;
  const indices = slot.indices;
  let vertexCount = 0;
  let indexCount = 0;
  const maxVertices = vertices.length / vertexStrideFloats;
  const maxIndices = indices.length;

  const clampX = (x: number): number => Math.max(0, Math.min(sizeX - 1, x));
  const clampZ = (z: number): number => Math.max(0, Math.min(sizeZ - 1, z));
  const voxelAt = (source: ChunkSlot, x: number, y: number, z: number): number => (
    source.voxels[(y * sizeZ + z) * sizeX + x]
  );
  const lightAt = (source: ChunkSlot, x: number, y: number, z: number): number => (
    source.light[(y * sizeZ + z) * sizeX + x]
  );

  const get = (x: number, y: number, z: number): number => {
    if (y < 0 || y >= sizeY) return 0;
    if (x < 0) {
      return neighborSlots.negX ? voxelAt(neighborSlots.negX, sizeX - 1, y, clampZ(z)) : 0;
    }
    if (x >= sizeX) {
      return neighborSlots.posX ? voxelAt(neighborSlots.posX, 0, y, clampZ(z)) : 0;
    }
    if (z < 0) {
      return neighborSlots.negZ ? voxelAt(neighborSlots.negZ, x, y, sizeZ - 1) : 0;
    }
    if (z >= sizeZ) {
      return neighborSlots.posZ ? voxelAt(neighborSlots.posZ, x, y, 0) : 0;
    }
    return voxelAt(slot, x, y, z);
  };

  const toIndex = (x: number, y: number, z: number): number => (y * sizeZ + z) * sizeX + x;

  const getVisualVariant = (blockId: number, voxelIndex: number): number => {
    const power = getPower(redstone[voxelIndex]);
    if (blockId === 55) return power;
    if (blockId === 75 || blockId === 76 || blockId === 93 || blockId === 123) return power > 0 ? 1 : 0;
    return 0;
  };

  const encodeSurface = (blockId: number, variant: number): number => blockId | (variant << VARIANT_SHIFT);

  const faceMaskValue = (
    a: number,
    b: number,
    aOwned: boolean,
    bOwned: boolean,
    ax: number,
    ay: number,
    az: number,
    bx: number,
    by: number,
    bz: number,
  ): number => {
    if (shouldCullFace(a, b, blocks)) return 0;
    if (aOwned && a !== 0) return encodeSurface(a, getVisualVariant(a, toIndex(ax, ay, az)));
    if (bOwned && b !== 0) return -encodeSurface(b, getVisualVariant(b, toIndex(bx, by, bz)));
    return 0;
  };

  const getLight = (x: number, y: number, z: number): number => {
    if (y < 0) return packLight(0, 0);
    if (y >= sizeY) return packLight(MAX_LIGHT, 0);
    if (x < 0) {
      return neighborSlots.negX ? lightAt(neighborSlots.negX, sizeX - 1, y, clampZ(z)) : lightAt(slot, 0, y, clampZ(z));
    }
    if (x >= sizeX) {
      return neighborSlots.posX ? lightAt(neighborSlots.posX, 0, y, clampZ(z)) : lightAt(slot, sizeX - 1, y, clampZ(z));
    }
    if (z < 0) {
      return neighborSlots.negZ ? lightAt(neighborSlots.negZ, x, y, sizeZ - 1) : lightAt(slot, x, y, 0);
    }
    if (z >= sizeZ) {
      return neighborSlots.posZ ? lightAt(neighborSlots.posZ, x, y, 0) : lightAt(slot, x, y, sizeZ - 1);
    }
    return lightAt(slot, x, y, z);
  };

  const occludes = (blockId: number): number => {
    if (blockId === 0) return 0;
    const block = blocks.tryGet(blockId);
    return block && block.material.opaque ? 1 : 0;
  };

  const maxMask = Math.max(sizeY * sizeZ, sizeX * sizeZ, sizeX * sizeY);
  const mask = new Int32Array(maxMask);
  const axes = [
    { d: Axis.X, u: Axis.Y, v: Axis.Z, sizeD: sizeX, sizeU: sizeY, sizeV: sizeZ },
    { d: Axis.Y, u: Axis.X, v: Axis.Z, sizeD: sizeY, sizeU: sizeX, sizeV: sizeZ },
    { d: Axis.Z, u: Axis.X, v: Axis.Y, sizeD: sizeZ, sizeU: sizeX, sizeV: sizeY },
  ] as const;

  for (const sweep of axes) {
    const { d, u, v, sizeD, sizeU, sizeV } = sweep;
    const x = [0, 0, 0];
    const q = [0, 0, 0];
    q[d] = 1;

    for (x[d] = -1; x[d] < sizeD;) {
      let n = 0;
      for (x[v] = 0; x[v] < sizeV; x[v]++) {
        for (x[u] = 0; x[u] < sizeU; x[u]++) {
          const aOwned = x[d] >= 0;
          const bOwned = x[d] + 1 < sizeD;
          const a = get(x[0], x[1], x[2]);
          const b = get(x[0] + q[0], x[1] + q[1], x[2] + q[2]);
          mask[n++] = faceMaskValue(
            a,
            b,
            aOwned,
            bOwned,
            x[0],
            x[1],
            x[2],
            x[0] + q[0],
            x[1] + q[1],
            x[2] + q[2],
          );
        }
      }
      x[d]++;

      n = 0;
      for (let j = 0; j < sizeV; j++) {
        for (let i = 0; i < sizeU;) {
          const cell = mask[n];
          if (cell === 0) {
            i++;
            n++;
            continue;
          }

          let width = 1;
          while (i + width < sizeU && mask[n + width] === cell) width++;

          let height = 1;
          let blocked = false;
          while (j + height < sizeV) {
            for (let k = 0; k < width; k++) {
              if (mask[n + k + height * sizeU] !== cell) {
                blocked = true;
                break;
              }
            }
            if (blocked) break;
            height++;
          }

          x[u] = i;
          x[v] = j;

          const du = [0, 0, 0];
          const dv = [0, 0, 0];
          du[u] = width;
          dv[v] = height;

          const p0 = [x[0], x[1], x[2]];
          const p1 = [x[0] + du[0], x[1] + du[1], x[2] + du[2]];
          const p2 = [x[0] + du[0] + dv[0], x[1] + du[1] + dv[1], x[2] + du[2] + dv[2]];
          const p3 = [x[0] + dv[0], x[1] + dv[1], x[2] + dv[2]];

          const sign = cell > 0 ? 1 : -1;
          const surface = Math.abs(cell);
          const blockId = surface & BLOCK_ID_MASK;
          const variant = surface >>> VARIANT_SHIFT;
          const block = blocks.get(blockId);
          const normal = [0, 0, 0];
          normal[d] = sign;
          const layer = getTextureLayer(blockId, variant, block, d, sign);
          const ao = computeCornerAO(get, occludes, x, du, dv, d, u, v, sign, width, height);
          const packedLight = computeCornerLight(getLight, x, du, dv, d, u, v, sign);
          const cornerW = (i: number): number =>
            ao[i] + getSkyLight(packedLight[i]) + getBlockLight(packedLight[i]);
          const flip = Math.abs(cornerW(1) - cornerW(3)) <= Math.abs(cornerW(0) - cornerW(2));

          if (vertexCount + 4 > maxVertices || indexCount + 6 > maxIndices) {
            slot.vertexCount[0] = vertexCount;
            slot.indexCount[0] = indexCount;
            return false;
          }

          writeVertex(
            vertices,
            vertexCount * vertexStrideFloats,
            p0,
            normal,
            [0, 0],
            layer,
            packVertexLight(getSkyLight(packedLight[0]), getBlockLight(packedLight[0]), ao[0]),
          );
          writeVertex(
            vertices,
            (vertexCount + 1) * vertexStrideFloats,
            p1,
            normal,
            [width, 0],
            layer,
            packVertexLight(getSkyLight(packedLight[1]), getBlockLight(packedLight[1]), ao[1]),
          );
          writeVertex(
            vertices,
            (vertexCount + 2) * vertexStrideFloats,
            p2,
            normal,
            [width, height],
            layer,
            packVertexLight(getSkyLight(packedLight[2]), getBlockLight(packedLight[2]), ao[2]),
          );
          writeVertex(
            vertices,
            (vertexCount + 3) * vertexStrideFloats,
            p3,
            normal,
            [0, height],
            layer,
            packVertexLight(getSkyLight(packedLight[3]), getBlockLight(packedLight[3]), ao[3]),
          );

          if (flip) {
            indices[indexCount++] = vertexCount;
            indices[indexCount++] = vertexCount + 1;
            indices[indexCount++] = vertexCount + 2;
            indices[indexCount++] = vertexCount;
            indices[indexCount++] = vertexCount + 2;
            indices[indexCount++] = vertexCount + 3;
          } else {
            indices[indexCount++] = vertexCount + 1;
            indices[indexCount++] = vertexCount + 2;
            indices[indexCount++] = vertexCount + 3;
            indices[indexCount++] = vertexCount + 1;
            indices[indexCount++] = vertexCount + 3;
            indices[indexCount++] = vertexCount;
          }
          vertexCount += 4;

          for (let h = 0; h < height; h++) {
            for (let w = 0; w < width; w++) {
              mask[n + w + h * sizeU] = 0;
            }
          }

          i += width;
          n += width;
        }
      }
    }
  }

  slot.vertexCount[0] = vertexCount;
  slot.indexCount[0] = indexCount;
  return true;
}

const computeCornerAO = (
  get: (x: number, y: number, z: number) => number,
  occludes: (blockId: number) => number,
  faceOrigin: number[],
  du: number[],
  dv: number[],
  d: Axis,
  u: Axis,
  v: Axis,
  sign: number,
  width: number,
  height: number,
): [number, number, number, number] => {
  const origin = [faceOrigin[0], faceOrigin[1], faceOrigin[2]];
  if (sign > 0) origin[d] += 1;

  const sample = (uDir: number, vDir: number): number => {
    const corner = [origin[0], origin[1], origin[2]];
    if (uDir > 0) {
      corner[0] += du[0];
      corner[1] += du[1];
      corner[2] += du[2];
    }
    if (vDir > 0) {
      corner[0] += dv[0];
      corner[1] += dv[1];
      corner[2] += dv[2];
    }

    const s1 = [corner[0], corner[1], corner[2]];
    const s2 = [corner[0], corner[1], corner[2]];
    const c = [corner[0], corner[1], corner[2]];
    s1[u] += uDir;
    s2[v] += vDir;
    c[u] += uDir;
    c[v] += vDir;
    return calculateAO(
      occludes(get(s1[0] | 0, s1[1] | 0, s1[2] | 0)),
      occludes(get(s2[0] | 0, s2[1] | 0, s2[2] | 0)),
      occludes(get(c[0] | 0, c[1] | 0, c[2] | 0)),
    );
  };

  void width;
  void height;
  return [
    sample(-1, -1),
    sample(1, -1),
    sample(1, 1),
    sample(-1, 1),
  ];
};

const getTextureLayer = (
  blockId: number,
  variant: number,
  block: ReturnType<BlockRegistry["get"]>,
  d: Axis,
  sign: number,
): number => {
  if (blockId === 55) return 46 + variant;
  if (blockId === 75 || blockId === 76 || blockId === 93) return 46 + Math.min(15, variant > 0 ? 15 : 0);
  if (blockId === 123) return variant > 0 ? 63 : 62;
  return d === Axis.Y ? (sign > 0 ? block.textures.top : block.textures.bottom) : block.textures.side;
};

const computeCornerLight = (
  getLight: (x: number, y: number, z: number) => number,
  faceOrigin: number[],
  du: number[],
  dv: number[],
  d: Axis,
  u: Axis,
  v: Axis,
  sign: number,
): [number, number, number, number] => {
  const origin = [faceOrigin[0], faceOrigin[1], faceOrigin[2]];
  if (sign > 0) origin[d] += 1;

  const sample = (uDir: number, vDir: number): number => {
    const corner = [origin[0], origin[1], origin[2]];
    if (uDir > 0) {
      corner[0] += du[0];
      corner[1] += du[1];
      corner[2] += du[2];
    }
    if (vDir > 0) {
      corner[0] += dv[0];
      corner[1] += dv[1];
      corner[2] += dv[2];
    }

    const s1 = [corner[0], corner[1], corner[2]];
    const s2 = [corner[0], corner[1], corner[2]];
    const c = [corner[0], corner[1], corner[2]];
    s1[u] += uDir;
    s2[v] += vDir;
    c[u] += uDir;
    c[v] += vDir;

    const l0 = getLight(corner[0] | 0, corner[1] | 0, corner[2] | 0);
    const l1 = getLight(s1[0] | 0, s1[1] | 0, s1[2] | 0);
    const l2 = getLight(s2[0] | 0, s2[1] | 0, s2[2] | 0);
    const l3 = getLight(c[0] | 0, c[1] | 0, c[2] | 0);
    const sky = Math.round((getSkyLight(l0) + getSkyLight(l1) + getSkyLight(l2) + getSkyLight(l3)) / 4);
    const block = Math.round((getBlockLight(l0) + getBlockLight(l1) + getBlockLight(l2) + getBlockLight(l3)) / 4);
    return packLight(sky, block);
  };

  return [
    sample(-1, -1),
    sample(1, -1),
    sample(1, 1),
    sample(-1, 1),
  ];
};

const writeVertex = (
  target: Float32Array,
  offset: number,
  position: number[],
  normal: number[],
  uv: [number, number],
  layer: number,
  packedLight: number,
): void => {
  target[offset + 0] = position[0];
  target[offset + 1] = position[1];
  target[offset + 2] = position[2];
  target[offset + 3] = normal[0];
  target[offset + 4] = normal[1];
  target[offset + 5] = normal[2];
  target[offset + 6] = uv[0];
  target[offset + 7] = uv[1];
  target[offset + 8] = layer;
  target[offset + 9] = packedLight;
};
