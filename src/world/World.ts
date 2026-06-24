import type { GameConfig } from "../engine/core/Config.js";
import { ChunkSlotStatus, SharedPool } from "../engine/alloc/SharedPool.js";
import type { WorkerHandle } from "../engine/workers/WorkerSpawner.js";
import type { MeshDoneMessage, WorldGenDoneMessage, WorldGenJobMessage, MeshJobMessage, WorkerOutboundMessage } from "../engine/workers/messages.js";
import type { SaveManager } from "../engine/save/SaveManager.js";
import { BlockRegistry } from "./BlockRegistry.js";
import { Chunk } from "./Chunk.js";
import { ChunkManager, packChunkKey } from "./ChunkManager.js";

const worldToChunk = (coordinate: number, size: number): number => Math.floor(coordinate / size);
const mod = (value: number, size: number): number => ((value % size) + size) % size;
const chunkSeed = (chunkX: number, chunkZ: number, seed: number): number => {
  let h = seed ^ Math.imul(chunkX, 374761393) ^ Math.imul(chunkZ, 668265263);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return h >>> 0;
};

export interface WorldBlockRef {
  readonly chunk: Chunk;
  readonly localX: number;
  readonly localZ: number;
  readonly index: number;
}

export class World {
  private readonly chunks = new ChunkManager();
  private readonly slotToChunk = new Map<number, Chunk>();
  private readonly pendingGen: Chunk[] = [];
  private readonly pendingMesh: Chunk[] = [];
  private saveManager: SaveManager | null = null;
  private centerChunkX = Number.NaN;
  private centerChunkZ = Number.NaN;

  constructor(
    private readonly pool: SharedPool,
    private readonly worldGenWorkers: WorkerHandle[],
    private readonly mesherWorkers: WorkerHandle[],
    private readonly blocks: BlockRegistry,
    private readonly config: GameConfig,
  ) {
    for (const handle of this.worldGenWorkers) {
      handle.worker.onmessage = (event: MessageEvent<WorkerOutboundMessage>) => {
        const message = event.data;
        if (message.kind === "generated") this.onWorldGenDone(handle, message);
      };
    }

    for (const handle of this.mesherWorkers) {
      handle.worker.onmessage = (event: MessageEvent<WorkerOutboundMessage>) => {
        const message = event.data;
        if (message.kind === "meshed") this.onMeshDone(handle, message);
      };
    }
  }

  update(cameraPosition: ArrayLike<number>): void {
    const chunkX = worldToChunk(cameraPosition[0], this.config.chunkSize);
    const chunkZ = worldToChunk(cameraPosition[2], this.config.chunkSize);

    this.centerChunkX = chunkX;
    this.centerChunkZ = chunkZ;
    this.ensureVisibleRadius(chunkX, chunkZ);
    this.unloadFarChunks(chunkX, chunkZ);

    this.pumpQueues();
  }

  dispose(): void {
    for (const handle of this.worldGenWorkers) handle.worker.terminate();
    for (const handle of this.mesherWorkers) handle.worker.terminate();
  }

  attachSaveManager(saveManager: SaveManager): void {
    this.saveManager = saveManager;
  }

  isReady(): boolean {
    if (!Number.isFinite(this.centerChunkX) || !Number.isFinite(this.centerChunkZ)) return false;
    const center = this.chunks.get(this.centerChunkX, this.centerChunkZ);
    return Boolean(center && (center.state === "meshReady" || center.state === "uploaded"));
  }

  hasChunkKey(key: string): boolean {
    return this.chunks.hasKey(key);
  }

  *entries(): IterableIterator<[string, Chunk]> {
    for (const chunk of this.chunks.values()) {
      yield [chunk.key, chunk];
    }
  }

  getChunkSlot(chunk: Chunk) {
    return this.pool.view(chunk.slotIndex);
  }

  getChunk(chunkX: number, chunkZ: number): Chunk | undefined {
    return this.chunks.get(chunkX, chunkZ);
  }

  getChunkBySlotIndex(slotIndex: number): Chunk | undefined {
    return this.slotToChunk.get(slotIndex);
  }

  getBlockIdAt(worldX: number, worldY: number, worldZ: number): number {
    if (worldY < 0 || worldY >= this.config.worldHeight) return 0;
    return this.getBlockId(worldX, worldY, worldZ);
  }

  getRedstonePackedAt(worldX: number, worldY: number, worldZ: number): number {
    const ref = this.resolveBlock(worldX, worldY, worldZ);
    if (!ref) return 0;
    return this.pool.view(ref.chunk.slotIndex).redstone[ref.index];
  }

  resolveBlock(worldX: number, worldY: number, worldZ: number): WorldBlockRef | null {
    if (worldY < 0 || worldY >= this.config.worldHeight) return null;
    const chunkX = worldToChunk(worldX, this.config.chunkSize);
    const chunkZ = worldToChunk(worldZ, this.config.chunkSize);
    const chunk = this.chunks.get(chunkX, chunkZ);
    if (!chunk) return null;
    const localX = mod(worldX, this.config.chunkSize);
    const localZ = mod(worldZ, this.config.chunkSize);
    const index = (worldY * this.config.chunkSize + localZ) * this.config.chunkSize + localX;
    return { chunk, localX, localZ, index };
  }

  setBlockIdAt(worldX: number, worldY: number, worldZ: number, blockId: number): boolean {
    const ref = this.resolveBlock(worldX, worldY, worldZ);
    if (!ref) return false;
    const slot = this.pool.view(ref.chunk.slotIndex);
    if (slot.voxels[ref.index] === blockId) return false;
    slot.voxels[ref.index] = blockId;
    if (blockId === 0) slot.redstone[ref.index] = 0;
    this.markChunkDirty(ref.chunk.chunkX, ref.chunk.chunkZ);
    this.requestRemesh(ref.chunk);
    return true;
  }

  setRedstonePackedAt(worldX: number, worldY: number, worldZ: number, packed: number): boolean {
    const ref = this.resolveBlock(worldX, worldY, worldZ);
    if (!ref) return false;
    const slot = this.pool.view(ref.chunk.slotIndex);
    if (slot.redstone[ref.index] === packed) return false;
    slot.redstone[ref.index] = packed;
    this.markChunkDirty(ref.chunk.chunkX, ref.chunk.chunkZ);
    this.requestRemesh(ref.chunk);
    return true;
  }

  markChunkDirty(chunkX: number, chunkZ: number): void {
    this.saveManager?.markDirty(chunkX, chunkZ);
  }

  requestRemesh(chunk: Chunk): void {
    if (chunk.state === "loadingFromDisk") return;
    if (chunk.state === "queuedMesh") return;
    if (chunk.state === "meshing") {
      chunk.needsRemesh = true;
      return;
    }
    if (
      chunk.state === "generating" ||
      chunk.state === "queuedGen" ||
      chunk.state === "voxelsReady"
    ) {
      return;
    }
    chunk.state = "queuedMesh";
    this.pendingMesh.push(chunk);
  }

  markUploaded(chunk: Chunk): void {
    chunk.state = "uploaded";
    const slot = this.pool.view(chunk.slotIndex);
    Atomics.store(slot.status, 0, ChunkSlotStatus.GPU_UPLOADED);
  }

  isSolid(worldX: number, worldY: number, worldZ: number): boolean {
    if (worldY < 0 || worldY >= this.config.worldHeight) return false;
    const blockId = this.getBlockId(worldX, worldY, worldZ);
    if (blockId === 0) return false;
    const block = this.blocks.tryGet(blockId);
    if (!block) return false;
    const { collision } = block;
    return collision.maxX > collision.minX && collision.maxY > collision.minY && collision.maxZ > collision.minZ;
  }

  isFluid(worldX: number, worldY: number, worldZ: number): boolean {
    if (worldY < 0 || worldY >= this.config.worldHeight) return false;
    const blockId = this.getBlockId(worldX, worldY, worldZ);
    if (blockId === 0) return false;
    const block = this.blocks.tryGet(blockId);
    return Boolean(block?.material.liquid);
  }

  onSaveLoadSuccess(
    chunkX: number,
    chunkZ: number,
    voxels: Uint8Array,
    light: Uint8Array,
    redstone: Uint8Array,
  ): void {
    const chunk = this.chunks.get(chunkX, chunkZ);
    if (!chunk) return;
    const slot = this.pool.view(chunk.slotIndex);
    Atomics.store(slot.chunkX, 0, chunkX);
    Atomics.store(slot.chunkZ, 0, chunkZ);
    slot.voxels.set(voxels);
    slot.light.set(light);
    slot.redstone.set(redstone);
    Atomics.store(slot.status, 0, ChunkSlotStatus.VOXELS_READY);
    chunk.state = "voxelsReady";
    chunk.needsRemesh = false;
    this.pendingMesh.push(chunk);
  }

  onSaveLoadFailed(chunkX: number, chunkZ: number): void {
    const chunk = this.chunks.get(chunkX, chunkZ);
    if (!chunk) return;
    chunk.state = "queuedGen";
    this.pendingGen.push(chunk);
  }

  private ensureVisibleRadius(centerChunkX: number, centerChunkZ: number): void {
    for (let dz = -this.config.renderDistance; dz <= this.config.renderDistance; dz++) {
      for (let dx = -this.config.renderDistance; dx <= this.config.renderDistance; dx++) {
        const chunkX = centerChunkX + dx;
        const chunkZ = centerChunkZ + dz;
        if (this.chunks.has(chunkX, chunkZ)) continue;

        const slot = this.pool.acquire();
        if (!slot) return;

        const chunk = new Chunk(chunkX, chunkZ, slot.slotIndex);
        Atomics.store(slot.chunkX, 0, chunkX);
        Atomics.store(slot.chunkZ, 0, chunkZ);
        this.chunks.set(chunk);
        this.slotToChunk.set(chunk.slotIndex, chunk);
        if (this.saveManager) {
          chunk.state = "loadingFromDisk";
          this.saveManager.requestLoad(chunkX, chunkZ);
        } else {
          chunk.state = "queuedGen";
          this.pendingGen.push(chunk);
        }
      }
    }
  }

  private unloadFarChunks(centerChunkX: number, centerChunkZ: number): void {
    const maxDistance = this.config.renderDistance + 1;
    const toUnload: Chunk[] = [];

    for (const chunk of this.chunks.values()) {
      const dx = Math.abs(chunk.chunkX - centerChunkX);
      const dz = Math.abs(chunk.chunkZ - centerChunkZ);
      const canRelease =
        chunk.state === "meshReady" ||
        chunk.state === "uploaded" ||
        chunk.state === "meshFailed";

      if ((dx > maxDistance || dz > maxDistance) && canRelease) {
        toUnload.push(chunk);
      }
    }

    for (const chunk of toUnload) {
      this.slotToChunk.delete(chunk.slotIndex);
      this.chunks.delete(chunk.chunkX, chunk.chunkZ);
      this.pool.release(this.pool.view(chunk.slotIndex));
    }
  }

  private pumpQueues(): void {
    for (const handle of this.worldGenWorkers) {
      if (handle.busy) continue;
      const chunk = this.pendingGen.shift();
      if (!chunk || this.slotToChunk.get(chunk.slotIndex) !== chunk) continue;
      const slot = this.pool.view(chunk.slotIndex);
      Atomics.store(slot.status, 0, ChunkSlotStatus.GENERATING);
      chunk.state = "generating";
      handle.busy = true;
      const message: WorldGenJobMessage = {
        kind: "generate",
        slotIndex: chunk.slotIndex,
        chunkX: chunk.chunkX,
        chunkZ: chunk.chunkZ,
        seed: chunkSeed(chunk.chunkX, chunk.chunkZ, this.config.worldSeed),
      };
      handle.worker.postMessage(message);
    }

    for (const handle of this.mesherWorkers) {
      if (handle.busy) continue;
      const chunk = this.pendingMesh.shift();
      if (!chunk || this.slotToChunk.get(chunk.slotIndex) !== chunk) continue;
      const slot = this.pool.view(chunk.slotIndex);
      Atomics.store(slot.status, 0, ChunkSlotStatus.MESHING);
      chunk.state = "meshing";
      handle.busy = true;
      const message: MeshJobMessage = {
        kind: "mesh",
        slotIndex: chunk.slotIndex,
      };
      handle.worker.postMessage(message);
    }
  }

  private onWorldGenDone(handle: WorkerHandle, message: WorldGenDoneMessage): void {
    handle.busy = false;
    const chunk = this.slotToChunk.get(message.slotIndex);
    if (!chunk) return;
    chunk.state = "voxelsReady";
    chunk.needsRemesh = false;
    this.saveManager?.markDirty(chunk.chunkX, chunk.chunkZ);
    this.pendingMesh.push(chunk);
  }

  private onMeshDone(handle: WorkerHandle, message: MeshDoneMessage): void {
    handle.busy = false;
    const chunk = this.slotToChunk.get(message.slotIndex);
    if (!chunk) return;
    chunk.vertexCount = message.vertexCount;
    chunk.indexCount = message.indexCount;
    chunk.state = message.success ? "meshReady" : "meshFailed";
    if (chunk.needsRemesh) {
      chunk.needsRemesh = false;
      this.requestRemesh(chunk);
    }
  }

  private getBlockId(worldX: number, worldY: number, worldZ: number): number {
    const chunkX = worldToChunk(worldX, this.config.chunkSize);
    const chunkZ = worldToChunk(worldZ, this.config.chunkSize);
    const chunk = this.chunks.get(chunkX, chunkZ);
    if (!chunk) return 0;

    const slot = this.pool.view(chunk.slotIndex);
    const localX = mod(worldX, this.config.chunkSize);
    const localZ = mod(worldZ, this.config.chunkSize);
    const index = (worldY * this.config.chunkSize + localZ) * this.config.chunkSize + localX;
    return slot.voxels[index];
  }
}
