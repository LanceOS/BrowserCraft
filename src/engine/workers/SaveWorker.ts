/// <reference lib="webworker" />

import type { SaveWorkerInboundMessage } from "../save/SaveMessages.js";
import { handleSaveWorkerMessage, type RegionRecord, type RegionStore } from "../save/SaveWorkerCore.js";

const ctx = self as unknown as DedicatedWorkerGlobalScope;
const DB_NAME = "VoxelSaveDB";
const STORE_NAME = "regions";

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
    const store: RegionStore = {
      getRegion: (id) => getRegion(db, id),
      putRegion: (record) => putRegion(db, record),
    };
    const result = await handleSaveWorkerMessage(msg, store);
    if (result.transfer) {
      ctx.postMessage(result.message, result.transfer);
    } else {
      ctx.postMessage(result.message);
    }
  } catch (error) {
    ctx.postMessage({
      type: msg.type === "SAVE_CHUNK" ? "SAVE_ERROR" : "LOAD_ERROR",
      chunkX: msg.chunkX,
      chunkZ: msg.chunkZ,
      reason: error instanceof Error ? error.message : String(error),
    });
  }
};
