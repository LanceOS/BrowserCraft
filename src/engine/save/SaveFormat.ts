export const SAVE_MAGIC = 0x564f5845;
export const SAVE_VERSION = 2;
export const SAVE_HEADER_BYTES = 32;

export interface SerializedChunkHeader {
  readonly magic: number;
  readonly chunkX: number;
  readonly chunkZ: number;
  readonly version: number;
  readonly payloadSize: number;
  readonly voxelBytes: number;
  readonly lightBytes: number;
  readonly redstoneBytes: number;
}

export interface DeserializedChunkData {
  readonly header: SerializedChunkHeader;
  readonly voxels: Uint8Array;
  readonly light: Uint8Array;
  readonly redstone: Uint8Array;
}

export function serializeChunkData(
  chunkX: number,
  chunkZ: number,
  voxels: Uint8Array,
  light: Uint8Array,
  redstone: Uint8Array,
): ArrayBuffer {
  const payloadSize = voxels.byteLength + light.byteLength + redstone.byteLength;
  const buffer = new ArrayBuffer(SAVE_HEADER_BYTES + payloadSize);
  const view = new DataView(buffer);
  view.setUint32(0, SAVE_MAGIC, true);
  view.setInt32(4, chunkX, true);
  view.setInt32(8, chunkZ, true);
  view.setUint32(12, SAVE_VERSION, true);
  view.setUint32(16, payloadSize, true);
  view.setUint32(20, voxels.byteLength, true);
  view.setUint32(24, light.byteLength, true);
  view.setUint32(28, redstone.byteLength, true);

  const bytes = new Uint8Array(buffer);
  bytes.set(voxels, SAVE_HEADER_BYTES);
  bytes.set(light, SAVE_HEADER_BYTES + voxels.byteLength);
  bytes.set(redstone, SAVE_HEADER_BYTES + voxels.byteLength + light.byteLength);
  return buffer;
}

export function deserializeChunkData(buffer: ArrayBuffer): DeserializedChunkData {
  const view = new DataView(buffer);
  const header: SerializedChunkHeader = {
    magic: view.getUint32(0, true),
    chunkX: view.getInt32(4, true),
    chunkZ: view.getInt32(8, true),
    version: view.getUint32(12, true),
    payloadSize: view.getUint32(16, true),
    voxelBytes: view.getUint32(20, true),
    lightBytes: view.getUint32(24, true),
    redstoneBytes: view.getUint32(28, true),
  };

  if (header.magic !== SAVE_MAGIC) {
    throw new Error("Invalid save magic");
  }
  if (header.version !== SAVE_VERSION) {
    throw new Error(`Unsupported save version ${header.version}`);
  }

  const voxels = new Uint8Array(buffer, SAVE_HEADER_BYTES, header.voxelBytes);
  const light = new Uint8Array(buffer, SAVE_HEADER_BYTES + header.voxelBytes, header.lightBytes);
  const redstone = new Uint8Array(
    buffer,
    SAVE_HEADER_BYTES + header.voxelBytes + header.lightBytes,
    header.redstoneBytes,
  );
  return { header, voxels, light, redstone };
}

export function estimateMaxCompressedSize(sourceBytes: number): number {
  return sourceBytes + Math.ceil(sourceBytes / 128) + 16;
}
