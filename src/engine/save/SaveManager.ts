import type { SharedPool } from "../alloc/SharedPool.js";
import { packChunkKey } from "../../world/ChunkManager.js";
import type { World } from "../../world/World.js";
import { deserializeChunkData, serializeChunkData } from "./SaveFormat.js";
import type { SaveWorkerInboundMessage, SaveWorkerOutboundMessage } from "./SaveMessages.js";

const REGION_SIZE = 32;

export class SaveManager {
  private readonly dirtyQueue = new Set<string>();
  private readonly pendingLoads = new Set<string>();
  private readonly pendingSaves = new Set<string>();
  private saveTimer = 0;
  private readonly saveInterval = 5;
  private readonly onWorkerMessage = (event: MessageEvent<SaveWorkerOutboundMessage>): void => {
    const msg = event.data;
    if (msg.type === "SAVE_COMPLETE") {
      this.pendingSaves.delete(packChunkKey(msg.chunkX, msg.chunkZ));
      return;
    }

    if (msg.type === "LOAD_SUCCESS") {
      this.pendingLoads.delete(packChunkKey(msg.chunkX, msg.chunkZ));
      const { header, voxels, light, redstone } = deserializeChunkData(msg.buffer);
      this.world.onSaveLoadSuccess(header.chunkX, header.chunkZ, voxels, light, redstone);
      return;
    }

    if (msg.type === "LOAD_FAILED" || msg.type === "LOAD_ERROR") {
      if ("chunkX" in msg && typeof msg.chunkX === "number" && typeof msg.chunkZ === "number") {
        this.pendingLoads.delete(packChunkKey(msg.chunkX, msg.chunkZ));
        this.world.onSaveLoadFailed(msg.chunkX, msg.chunkZ);
      }
      if (msg.type === "LOAD_ERROR") {
        console.warn("Save load error:", msg.reason);
      }
      return;
    }

    if (msg.type === "SAVE_ERROR") {
      if (typeof msg.chunkX === "number" && typeof msg.chunkZ === "number") {
        this.pendingSaves.delete(packChunkKey(msg.chunkX, msg.chunkZ));
      }
      console.warn("Save write error:", msg.reason);
    }
  };

  constructor(
    private readonly worker: Worker,
    private readonly sharedPool: SharedPool,
    private readonly world: World,
    private readonly worldId: string,
  ) {
    this.worker.addEventListener("message", this.onWorkerMessage);
  }

  markDirty(chunkX: number, chunkZ: number): void {
    this.dirtyQueue.add(packChunkKey(chunkX, chunkZ));
  }

  update(dt: number): void {
    this.saveTimer += dt;
    if (this.saveTimer < this.saveInterval || this.dirtyQueue.size === 0) return;

    this.dispatchDirtySaves(10);

    if (this.dirtyQueue.size === 0) {
      this.saveTimer = 0;
    }
  }

  flushPending(): void {
    this.dispatchDirtySaves(Number.POSITIVE_INFINITY);
    if (this.dirtyQueue.size === 0) {
      this.saveTimer = 0;
    }
  }

  requestLoad(chunkX: number, chunkZ: number): void {
    const key = packChunkKey(chunkX, chunkZ);
    if (this.pendingLoads.has(key)) return;
    this.pendingLoads.add(key);
    const message: SaveWorkerInboundMessage = {
      type: "LOAD_CHUNK",
      worldId: this.worldId,
      chunkX,
      chunkZ,
      regionX: Math.floor(chunkX / REGION_SIZE),
      regionZ: Math.floor(chunkZ / REGION_SIZE),
    };
    this.worker.postMessage(message);
  }

  dispose(): void {
    this.worker.removeEventListener("message", this.onWorkerMessage);
    this.worker.terminate();
  }

  private dispatchDirtySaves(maxSaves: number): void {
    let savedCount = 0;

    for (const key of this.dirtyQueue) {
      if (savedCount >= maxSaves) break;
      const [xStr, zStr] = key.split(":");
      const chunkX = Number.parseInt(xStr, 10);
      const chunkZ = Number.parseInt(zStr, 10);
      const chunk = this.world.getChunk(chunkX, chunkZ);
      if (!chunk) {
        this.dirtyQueue.delete(key);
        continue;
      }

      const canSave =
        chunk.state === "uploaded" ||
        chunk.state === "meshReady" ||
        chunk.state === "voxelsReady";
      if (!canSave) continue;

      const slot = this.sharedPool.view(chunk.slotIndex);
      const rawBuffer = serializeChunkData(chunkX, chunkZ, slot.voxels, slot.light, slot.redstone);
      const regionX = Math.floor(chunkX / REGION_SIZE);
      const regionZ = Math.floor(chunkZ / REGION_SIZE);
      const message: SaveWorkerInboundMessage = {
        type: "SAVE_CHUNK",
        worldId: this.worldId,
        chunkX,
        chunkZ,
        regionX,
        regionZ,
        rawBuffer,
      };
      this.pendingSaves.add(key);
      this.worker.postMessage(message, [rawBuffer]);
      this.dirtyQueue.delete(key);
      savedCount++;
    }
  }
}
