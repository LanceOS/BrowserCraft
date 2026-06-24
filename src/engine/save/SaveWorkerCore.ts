import { compressRLE, decompressRLE } from "./RLE.js";
import { estimateMaxCompressedSize } from "./SaveFormat.js";
import type { SaveWorkerInboundMessage, SaveWorkerOutboundMessage } from "./SaveMessages.js";

export interface StoredChunkRecord {
  readonly rawSize: number;
  readonly buffer: ArrayBuffer;
}

export interface RegionRecord {
  readonly id: string;
  readonly chunks: Record<string, StoredChunkRecord>;
}

export interface RegionStore {
  getRegion(id: string): Promise<RegionRecord | undefined>;
  putRegion(record: RegionRecord): Promise<void>;
}

export interface SaveWorkerCoreResult {
  readonly message: SaveWorkerOutboundMessage;
  readonly transfer?: Transferable[];
}

export const regionKey = (worldId: string, regionX: number, regionZ: number): string =>
  `${worldId}|${regionX}|${regionZ}`;

export const localChunkKey = (chunkX: number, chunkZ: number): string => {
  const localX = ((chunkX % 32) + 32) % 32;
  const localZ = ((chunkZ % 32) + 32) % 32;
  return `${localX}:${localZ}`;
};

export async function handleSaveWorkerMessage(
  msg: SaveWorkerInboundMessage,
  store: RegionStore,
): Promise<SaveWorkerCoreResult> {
  try {
    if (msg.type === "SAVE_CHUNK") {
      const source = new Uint8Array(msg.rawBuffer);
      const compressed = new Uint8Array(estimateMaxCompressedSize(source.byteLength));
      const compressedLength = compressRLE(source, compressed);
      const id = regionKey(msg.worldId, msg.regionX, msg.regionZ);
      const chunkKey = localChunkKey(msg.chunkX, msg.chunkZ);
      const existing = (await store.getRegion(id)) ?? { id, chunks: {} };
      const next: RegionRecord = {
        id,
        chunks: {
          ...existing.chunks,
          [chunkKey]: {
            rawSize: source.byteLength,
            buffer: compressed.buffer.slice(0, compressedLength),
          },
        },
      };
      await store.putRegion(next);
      return {
        message: {
          type: "SAVE_COMPLETE",
          chunkX: msg.chunkX,
          chunkZ: msg.chunkZ,
        },
      };
    }

    const id = regionKey(msg.worldId, msg.regionX, msg.regionZ);
    const region = await store.getRegion(id);
    const chunk = region?.chunks[localChunkKey(msg.chunkX, msg.chunkZ)];
    if (!chunk) {
      return {
        message: {
          type: "LOAD_FAILED",
          chunkX: msg.chunkX,
          chunkZ: msg.chunkZ,
        },
      };
    }

    const decompressed = new Uint8Array(chunk.rawSize);
    decompressRLE(new Uint8Array(chunk.buffer), decompressed);
    return {
      message: {
        type: "LOAD_SUCCESS",
        chunkX: msg.chunkX,
        chunkZ: msg.chunkZ,
        buffer: decompressed.buffer,
      },
      transfer: [decompressed.buffer],
    };
  } catch (error) {
    return {
      message: {
        type: msg.type === "SAVE_CHUNK" ? "SAVE_ERROR" : "LOAD_ERROR",
        chunkX: msg.chunkX,
        chunkZ: msg.chunkZ,
        reason: error instanceof Error ? error.message : String(error),
      },
    };
  }
}
