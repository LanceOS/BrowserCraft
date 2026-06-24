# Voxel Engine Technical Design Document: Chunk Serialization & Save/Load



**Version:** 1.0**Scope:** Region-based Persistent Storage, RLE Compression, IndexedDB Integration, and Non-Blocking I/O.**Architecture Constraints:** Strict TypeScript, Data-Oriented Design (TypedArrays only), Zero-Copy Web Worker boundaries, No main-thread blocking.


---

## 1. System Overview

Saving a voxel world requires persisting millions of block states to disk (browser storage). If we save the raw `Uint8Array` for every chunk (65,536 bytes for voxels + 65,536 bytes for light), a render distance of 16 would consume \~125MB per save.

To solve this, the `SaveManager` groups chunks into **32x32 Region Files** (mirroring classic Minecraft `.mca` format concepts). Each region file is compressed using a custom, allocation-free **Run-Length Encoding (RLE)** algorithm.

All serialization, compression, and IndexedDB I/O happen inside a dedicated **SaveWorker** to guarantee the main thread never stalls.


---

## 2. Serialization Format (Bit-Packed RLE)

A chunk column (16x16x256) is highly stratified. Large contiguous runs of Air (0) and Stone (1) dominate the volume.

We use a control-byte RLE scheme:

* If `controlByte < 128`: The next `controlByte + 1` bytes are literal block IDs.
* If `controlByte >= 128`: The next byte is a block ID repeated `(controlByte - 128) + 3` times (minimum run of 3).

### 2.1 Serialization Header

```typescript
// /src/engine/save/SaveFormat.ts

/** Magic bytes for save file validation */
export const SAVE_MAGIC = 0x564F5845; // "VOXE"

/** Layout of a serialized chunk buffer (before RLE compression) */
export interface SerializedChunkHeader {
  magic: number;       // 4 bytes
  chunkX: number;      // 4 bytes (Int32)
  chunkZ: number;      // 4 bytes (Int32)
  version: number;     // 1 byte (format version)
  compressedSize: number; // 4 bytes (Int32)
}
```

### 2.2 Allocation-Free RLE Algorithm

This algorithm compresses a `Uint8Array` into a pre-allocated `ArrayBuffer` using pure pointer arithmetic. Zero intermediate arrays are created.

```typescript
// /src/engine/save/RLE.ts

/**
 * Compresses a Uint8Array using RLE.
 * O(N) complexity. Zero allocations.
 * 
 * @param src The raw voxel/light data.
 * @param dst The pre-allocated destination buffer.
 * @returns The number of bytes written to dst.
 */
export function compressRLE(src: Uint8Array, dst: Uint8Array): number {
  let srcIdx = 0;
  let dstIdx = 0;
  const srcLen = src.length;

  while (srcIdx < srcLen) {
    // Find run length
    const runStart = srcIdx;
    const val = src[srcIdx];
    srcIdx++;
    while (srcIdx < srcLen && src[srcIdx] === val && (srcIdx - runStart) < 130) {
      srcIdx++;
    }
    const runLen = srcIdx - runStart;

    if (runLen >= 3) {
      // Write Run: Control byte (>=128) + Value byte
      dst[dstIdx++] = 128 + (runLen - 3);
      dst[dstIdx++] = val;
    } else {
      // Literal run: search ahead to see how long the literal is (max 127)
      let litLen = runLen;
      while (srcIdx < srcLen && 
             src[srcIdx] !== src[srcIdx - 1] && // Stop if a new run starts
             litLen < 127) {
        dst[dstIdx + litLen + 1] = src[srcIdx - 1]; // write literal behind control byte
        srcIdx++;
        litLen++;
      }
      // Control byte (<128) + Literals
      dst[dstIdx++] = litLen - 1;
      dstIdx += litLen;
    }
  }
  return dstIdx;
}

/**
 * Decompresses RLE data into a pre-allocated Uint8Array.
 * O(N) complexity. Zero allocations.
 */
export function decompressRLE(src: Uint8Array, dst: Uint8Array): void {
  let srcIdx = 0;
  let dstIdx = 0;
  const srcLen = src.length;

  while (srcIdx < srcLen && dstIdx < dst.length) {
    const ctrl = src[srcIdx++];
    if (ctrl < 128) {
      // Literal run
      const len = ctrl + 1;
      for (let i = 0; i < len; i++) {
        dst[dstIdx++] = src[srcIdx++];
      }
    } else {
      // Repeated run
      const len = (ctrl - 128) + 3;
      const val = src[srcIdx++];
      for (let i = 0; i < len; i++) {
        dst[dstIdx++] = val;
      }
    }
  }
}
```


---

## 3. The SaveWorker (IndexedDB Integration)

To avoid blocking, the Main Thread transfers ownership of `ArrayBuffer`s to the `SaveWorker` using `Transferable` objects. The worker compresses the data and writes it to an `IndexedDB` Object Store keyed by `"regionX|regionZ"`.

```typescript
// /src/engine/workers/SaveWorker.ts
/// <reference lib="webworker" />

import { compressRLE, decompressRLE } from "../save/RLE";

const ctx = self as unknown as DedicatedWorkerGlobalScope;
let db: IDBDatabase | null = null;

// Initialize IndexedDB
const request = indexedDB.open("VoxelSaveDB", 1);
request.onupgradeneeded = () => {
  db = request.result;
  if (!db.objectStoreNames.contains("regions")) {
    db.createObjectStore("regions", { keyPath: "id" });
  }
};
request.onsuccess = () => { db = request.result; };

ctx.onmessage = (e: MessageEvent<SaveWorkerMessage>) => {
  if (!db) return;
  const msg = e.data;

  if (msg.type === "SAVE_CHUNK") {
    // 1. Compress the transferred buffer
    const compressed = new Uint8Array(msg.maxCompressedSize);
    const compressedLen = compressRLE(new Uint8Array(msg.rawBuffer), compressed);

    // 2. Store in IndexedDB
    const tx = db.transaction("regions", "readwrite");
    const store = tx.objectStore("regions");
    
    // We use a put operation to overwrite/insert the region
    const regionData = { 
      id: `${msg.regionX}|${msg.regionZ}`, 
      buffer: compressed.buffer.slice(0, compressedLen) // Copy out the used portion
    };
    store.put(regionData);

  } else if (msg.type === "LOAD_CHUNK") {
    // 1. Fetch Region from IndexedDB
    const tx = db.transaction("regions", "readonly");
    const store = tx.objectStore("regions");
    const req = store.get(`${msg.regionX}|${msg.regionZ}`);
    
    req.onsuccess = () => {
      const result = req.result;
      if (!result) {
        // Region not found, signal main thread to generate
        ctx.postMessage({ type: "LOAD_FAILED", chunkX: msg.chunkX, chunkZ: msg.chunkZ });
        return;
      }

      // 2. Decompress and Transfer back to Main Thread
      const decompressed = new Uint8Array(65536); // Chunk volume
      decompressRLE(new Uint8Array(result.buffer), decompressed);

      ctx.postMessage({ 
        type: "LOAD_SUCCESS", 
        chunkX: msg.chunkX, 
        chunkZ: msg.chunkZ, 
        buffer: decompressed.buffer 
      }, [decompressed.buffer]); // Transfer ownership
    };
  }
};

interface SaveWorkerMessage {
  type: "SAVE_CHUNK" | "LOAD_CHUNK";
  // Save props
  rawBuffer?: ArrayBuffer;
  maxCompressedSize?: number;
  // Load props
  chunkX?: number;
  chunkZ?: number;
  regionX?: number;
  regionZ?: number;
}
```


---

## 4. Main Thread SaveManager (DOD Orchestrator)

The `SaveManager` runs on the main thread. It maintains a **Dirty Queue** of chunks modified by the player. Every N seconds, it dequeues a batch of dirty chunks, extracts their voxel data from the `SharedArrayBuffer`, and transfers it to the SaveWorker.

```typescript
// /src/engine/save/SaveManager.ts

export class SaveManager {
  // Set of "chunkX|chunkZ" strings that need saving
  private readonly dirtyQueue: Set<string> = new Set();
  private saveTimer: number = 0;
  private readonly saveInterval: number = 5.0; // Save every 5 seconds

  constructor(
    private readonly worker: Worker,
    private readonly sharedPool: SharedPool,
    private readonly chunkManager: ChunkManager
  ) {}

  /** Called by BlockInteractionSystem when a block is broken/placed */
  public markDirty(chunkX: number, chunkZ: number): void {
    this.dirtyQueue.add(`${chunkX}|${chunkZ}`);
  }

  update(dt: number): void {
    this.saveTimer += dt;
    if (this.saveTimer < this.saveInterval || this.dirtyQueue.size === 0) return;

    // Batch save up to 10 chunks per frame to avoid SharedArrayBuffer read spikes
    let savedCount = 0;
    const MAX_SAVES_PER_TICK = 10;

    for (const key of this.dirtyQueue) {
      if (savedCount >= MAX_SAVES_PER_TICK) break;

      const [xStr, zStr] = key.split("|");
      const chunkX = parseInt(xStr, 10);
      const chunkZ = parseInt(zStr, 10);

      const chunk = this.chunkManager.getChunk(chunkX, chunkZ);
      if (!chunk || chunk.status !== ChunkSlotStatus.GPU_UPLOADED) continue;

      // 1. Extract data from SharedArrayBuffer into a temporary transfer buffer
      const transferBuffer = new ArrayBuffer(65536);
      const transferView = new Uint8Array(transferBuffer);
      const slot = this.sharedPool.view(chunk.slotIndex);
      
      // Copy voxel data (light data omitted for brevity, but usually saved too)
      transferView.set(new Uint8Array(slot.buffer, slot.baseByteOffset + 32, 65536));

      // 2. Calculate Region coordinates (32x32 chunks per region)
      const regionX = Math.floor(chunkX / 32);
      const regionZ = Math.floor(chunkZ / 32);

      // 3. Dispatch to Worker (Transfer ownership of transferBuffer)
      this.worker.postMessage({
        type: "SAVE_CHUNK",
        rawBuffer: transferBuffer,
        maxCompressedSize: 65536, // Worst case
        chunkX, chunkZ, regionX, regionZ
      }, [transferBuffer]);

      this.dirtyQueue.delete(key);
      savedCount++;
    }

    if (this.dirtyQueue.size === 0) {
      this.saveTimer = 0; // Reset timer if queue is cleared
    }
  }

  /**
   * Requests a chunk load from disk. 
   * Returns true if the load was dispatched, false if it doesn't exist.
   */
  public requestLoad(chunkX: number, chunkZ: number): boolean {
    const regionX = Math.floor(chunkX / 32);
    const regionZ = Math.floor(chunkZ / 32);

    // Post message to worker. Worker will eventually reply with LOAD_SUCCESS or LOAD_FAILED
    this.worker.postMessage({
      type: "LOAD_CHUNK",
      chunkX, chunkZ, regionX, regionZ
    });
    return true;
  }
}
```


---

## 5. Load Integration with WorldGen

When the `ChunkManager` needs a new chunk, it doesn't immediately send it to the `WorldGenWorker`. It first queries the `SaveManager`.

```typescript
// /src/world/ChunkManager.ts (Excerpt)

public requestChunk(chunkX: number, chunkZ: number): void {
  // 1. Check if already loaded/loading
  if (this.chunks.has(`${chunkX}|${chunkZ}`)) return;

  // 2. Request from SaveManager
  this.saveManager.requestLoad(chunkX, chunkZ);
  
  // Chunk state is now "LOADING_FROM_DISK"
  // The SaveWorker will respond asynchronously...
}
```

When the `SaveWorker` responds with `LOAD_SUCCESS`, the Main Thread event listener injects the data directly into the `SharedArrayBuffer` pool and skips straight to the `MesherWorker`.

```typescript
// /src/game/Game.ts (Worker Event Listener Excerpt)

saveWorker.onmessage = (e: MessageEvent) => {
  const msg = e.data;
  if (msg.type === "LOAD_SUCCESS") {
    // 1. Acquire a slot in the SharedPool
    const slot = this.sharedPool.acquire();
    if (!slot) return; // Pool full, wait

    // 2. Copy decompressed data from the transferred buffer into the SharedArrayBuffer
    const loadedVoxels = new Uint8Array(msg.buffer);
    const slotVoxels = new Uint8Array(
      slot.buffer, 
      slot.baseByteOffset + 32, // Skip header
      65536
    );
    slotVoxels.set(loadedVoxels);

    // 3. Skip WorldGen, dispatch directly to Mesher
    Atomics.store(slot.chunkX, 0, msg.chunkX);
    Atomics.store(slot.chunkZ, 0, msg.chunkZ);
    Atomics.store(slot.status, 0, ChunkSlotStatus.VOXELS_READY);
    
    this.mesherWorker.postMessage({ slotIndex: slot.slotIndex });
  } else if (msg.type === "LOAD_FAILED") {
    // 2. Chunk doesn't exist on disk. Dispatch to WorldGenWorker.
    this.worldGenWorker.postMessage({ slotIndex: /* allocate slot */, chunkX: msg.chunkX, chunkZ: msg.chunkZ });
  }
};
```

### Summary of Save/Load Compliance


1. **Zero Main-Thread Blocking:** All disk I/O (`IndexedDB` transactions) and CPU-intensive compression (`RLE`) are isolated in the `SaveWorker`. The Main Thread only reads from the `SharedArrayBuffer` and transfers memory.
2. **Zero-Copy Transfers:** Data moves between threads via `Transferable` ArrayBuffers. When the Main Thread sends data to the `SaveWorker`, it transfers the underlying `ArrayBuffer` (`[transferBuffer]`), avoiding structured cloning overhead.
3. **DOD Storage:** Chunks are not deserialized into `class Chunk` objects. The `SaveWorker` transfers a raw `ArrayBuffer` back to the Main Thread, which writes it directly into the `SharedArrayBuffer` using `TypedArray.set()`. The data is immediately ready for the Mesher.
4. **Region Batching:** By saving chunks into 32x32 regions within IndexedDB, we minimize the number of database keys and leverage browser storage optimization for large binary blobs.


