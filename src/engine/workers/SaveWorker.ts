/// <reference lib="webworker" />

import { compressRLE, decompressRLE } from "../save/RLE.js";
import { estimateMaxCompressedSize } from "../save/SaveFormat.js";
import type { SaveWorkerInboundMessage, SaveWorkerOutboundMessage } from "../save/SaveMessages.js";

const ctx = self as unknown as DedicatedWorkerGlobalScope;
const DB_NAME = "VoxelSaveDB";
const STORE_NAME = "regions";

interface StoredChunkRecord {
  readonly rawSize: number;
  readonly buffer: ArrayBuffer;
}

interface RegionRecord {
  readonly id: string;
  readonly chunks: Record<string, StoredChunkRecord>;
}

const openDatabase = (): Promise<IDBDatabase> =>
  new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, 1);
    request.onupgradeneeded = () => {
      const db = request.result;
      if (!db.objectStoreNames.contains(STORE_NAME)) {
        db.createObjectStore(STORE_NAME, { keyPath: "id" });
      }
    };
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error ?? new Error("Failed to open save database"));
  });

const dbPromise = openDatabase();

const regionKey = (worldId: string, regionX: number, regionZ: number): string => `${worldId}|${regionX}|${regionZ}`;
const localChunkKey = (chunkX: number, chunkZ: number): string => {
  const localX = ((chunkX % 32) + 32) % 32;
  const localZ = ((chunkZ % 32) + 32) % 32;
  return `${localX}:${localZ}`;
};

const getRegion = (db: IDBDatabase, id: string): Promise<RegionRecord | undefined> =>
  new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_NAME, "readonly");
    const req = tx.objectStore(STORE_NAME).get(id);
    req.onsuccess = () => resolve(req.result as RegionRecord | undefined);
    req.onerror = () => reject(req.error ?? new Error("Failed to read region"));
  });

const putRegion = (db: IDBDatabase, record: RegionRecord): Promise<void> =>
  new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_NAME, "readwrite");
    const req = tx.objectStore(STORE_NAME).put(record);
    req.onsuccess = () => resolve();
    req.onerror = () => reject(req.error ?? new Error("Failed to write region"));
  });

ctx.onmessage = async (event: MessageEvent<SaveWorkerInboundMessage>) => {
  const msg = event.data;

  try {
    const db = await dbPromise;
    if (msg.type === "SAVE_CHUNK") {
      const source = new Uint8Array(msg.rawBuffer);
      const compressed = new Uint8Array(estimateMaxCompressedSize(source.byteLength));
      const compressedLength = compressRLE(source, compressed);
      const id = regionKey(msg.worldId, msg.regionX, msg.regionZ);
      const chunkKey = localChunkKey(msg.chunkX, msg.chunkZ);
      const existing = (await getRegion(db, id)) ?? { id, chunks: {} };
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
      await putRegion(db, next);
      const response: SaveWorkerOutboundMessage = {
        type: "SAVE_COMPLETE",
        chunkX: msg.chunkX,
        chunkZ: msg.chunkZ,
      };
      ctx.postMessage(response);
      return;
    }

    const id = regionKey(msg.worldId, msg.regionX, msg.regionZ);
    const region = await getRegion(db, id);
    const chunk = region?.chunks[localChunkKey(msg.chunkX, msg.chunkZ)];
    if (!chunk) {
      const response: SaveWorkerOutboundMessage = {
        type: "LOAD_FAILED",
        chunkX: msg.chunkX,
        chunkZ: msg.chunkZ,
      };
      ctx.postMessage(response);
      return;
    }

    const decompressed = new Uint8Array(chunk.rawSize);
    decompressRLE(new Uint8Array(chunk.buffer), decompressed);
    const response: SaveWorkerOutboundMessage = {
      type: "LOAD_SUCCESS",
      chunkX: msg.chunkX,
      chunkZ: msg.chunkZ,
      buffer: decompressed.buffer,
    };
    ctx.postMessage(response, [decompressed.buffer]);
  } catch (error) {
    const response: SaveWorkerOutboundMessage = {
      type: msg.type === "SAVE_CHUNK" ? "SAVE_ERROR" : "LOAD_ERROR",
      chunkX: msg.chunkX,
      chunkZ: msg.chunkZ,
      reason: error instanceof Error ? error.message : String(error),
    };
    ctx.postMessage(response);
  }
};
