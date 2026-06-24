import type { SharedPool, ChunkSlot } from "../../alloc/SharedPool.js";
import type { System } from "../SystemManager.js";
import type { World } from "../../../world/World.js";
import { getPower, getState, packRedstone } from "../../workers/redstone/RedstonePacker.js";

const CHUNK_EDGE = 15;
const enum LogicBlock {
  WIRE = 55,
  TORCH_OFF = 75,
  TORCH_ON = 76,
  REPEATER = 93,
  LAMP = 123,
}

export class RedstoneSystem<TState> implements System<TState> {
  readonly name = "redstone";
  readonly stage = "postPhysics" as const;
  private readonly queue: Int32Array;
  private queueHead = 0;
  private queueTail = 0;
  private readonly tickRate = 10;
  private accumulator = 0;

  constructor(
    private readonly sharedPool: SharedPool,
    private readonly world: World,
    capacity = 16384,
  ) {
    this.queue = new Int32Array(capacity);
  }

  update(_state: TState, dt: number): void {
    this.accumulator += dt;
    const tickInterval = 1 / this.tickRate;
    while (this.accumulator >= tickInterval) {
      this.accumulator -= tickInterval;
      this.processTick();
    }
  }

  enqueue(slotIndex: number, x: number, y: number, z: number, power: number): void {
    const packed = (slotIndex << 22) | ((power & 0x0f) << 18) | ((y & 0xff) << 10) | ((z & 0x1f) << 5) | (x & 0x1f);
    const nextTail = (this.queueTail + 1) % this.queue.length;
    if (nextTail === this.queueHead) {
      console.warn("Redstone queue overflow; dropping update");
      return;
    }
    this.queue[this.queueTail] = packed;
    this.queueTail = nextTail;
  }

  triggerAtWorld(worldX: number, worldY: number, worldZ: number, power: number): boolean {
    const cell = this.world.resolveBlock(worldX, worldY, worldZ);
    if (!cell) return false;
    this.enqueue(cell.chunk.slotIndex, cell.localX, worldY, cell.localZ, power);
    return true;
  }

  private dequeue(): number | null {
    if (this.queueHead === this.queueTail) return null;
    const value = this.queue[this.queueHead];
    this.queueHead = (this.queueHead + 1) % this.queue.length;
    return value;
  }

  private processTick(): void {
    const updatesThisTick =
      this.queueTail >= this.queueHead
        ? this.queueTail - this.queueHead
        : this.queue.length - this.queueHead + this.queueTail;

    for (let i = 0; i < updatesThisTick; i++) {
      const packed = this.dequeue();
      if (packed === null) break;

      const slotIndex = (packed >>> 22) & 0x3ff;
      const incomingPower = (packed >>> 18) & 0x0f;
      const y = (packed >>> 10) & 0xff;
      const z = (packed >>> 5) & 0x1f;
      const x = packed & 0x1f;

      const chunk = this.world.getChunkBySlotIndex(slotIndex);
      if (!chunk) continue;
      const slot = this.sharedPool.view(slotIndex);
      const idx = (y * this.sharedPool.dimensions.sizeZ + z) * this.sharedPool.dimensions.sizeX + x;
      const blockId = slot.voxels[idx];
      if (!this.isLogicBlock(blockId)) continue;

      const currentPacked = slot.redstone[idx];
      const evaluatedPower = this.evaluateComponent(blockId, x, y, z, incomingPower, slot);
      if (evaluatedPower === getPower(currentPacked)) continue;

      slot.redstone[idx] = packRedstone(evaluatedPower, getState(currentPacked));
      this.world.requestRemesh(chunk);
      this.world.markChunkDirty(chunk.chunkX, chunk.chunkZ);
      this.enqueueNeighbors(slotIndex, x, y, z, evaluatedPower);
    }
  }

  private evaluateComponent(
    blockId: number,
    x: number,
    y: number,
    z: number,
    incomingPower: number,
    slot: ChunkSlot,
  ): number {
    switch (blockId) {
      case LogicBlock.WIRE: {
        let maxNeighborPower = Math.max(0, incomingPower - 1);
        const neighbors = this.getNeighborSignals(x, y, z, slot);
        for (let i = 0; i < neighbors.length; i++) {
          const neighbor = neighbors[i];
          if (neighbor.id === LogicBlock.WIRE) {
            maxNeighborPower = Math.max(maxNeighborPower, Math.max(0, neighbor.power - 1));
          } else if (this.isEmitter(neighbor.id, neighbor.power)) {
            maxNeighborPower = Math.max(maxNeighborPower, 15);
          } else if (neighbor.id === LogicBlock.LAMP && neighbor.power > 0) {
            maxNeighborPower = Math.max(maxNeighborPower, neighbor.power);
          }
        }
        return Math.min(15, maxNeighborPower);
      }
      case LogicBlock.TORCH_ON:
      case LogicBlock.TORCH_OFF:
        return incomingPower > 0 ? 0 : 15;
      case LogicBlock.REPEATER:
        return incomingPower > 0 ? 15 : 0;
      case LogicBlock.LAMP:
        return incomingPower > 0 ? incomingPower : 0;
      default:
        return incomingPower;
    }
  }

  private enqueueNeighbors(slotIndex: number, x: number, y: number, z: number, power: number): void {
    if (x > 0) this.enqueue(slotIndex, x - 1, y, z, power);
    if (x < CHUNK_EDGE) this.enqueue(slotIndex, x + 1, y, z, power);
    if (z > 0) this.enqueue(slotIndex, x, y, z - 1, power);
    if (z < CHUNK_EDGE) this.enqueue(slotIndex, x, y, z + 1, power);
    if (y > 0) this.enqueue(slotIndex, x, y - 1, z, power);
    if (y < this.sharedPool.dimensions.sizeY - 1) this.enqueue(slotIndex, x, y + 1, z, power);
  }

  private getNeighborSignals(x: number, y: number, z: number, slot: ChunkSlot): Array<{ id: number; power: number }> {
    const out: Array<{ id: number; power: number }> = [];
    const sizeX = this.sharedPool.dimensions.sizeX;
    const sizeZ = this.sharedPool.dimensions.sizeZ;
    const pushNeighbor = (nx: number, ny: number, nz: number): void => {
      if (nx < 0 || nx >= sizeX || nz < 0 || nz >= sizeZ || ny < 0 || ny >= this.sharedPool.dimensions.sizeY) return;
      const idx = (ny * sizeZ + nz) * sizeX + nx;
      out.push({ id: slot.voxels[idx], power: getPower(slot.redstone[idx]) });
    };

    pushNeighbor(x - 1, y, z);
    pushNeighbor(x + 1, y, z);
    pushNeighbor(x, y, z - 1);
    pushNeighbor(x, y, z + 1);
    pushNeighbor(x, y - 1, z);
    pushNeighbor(x, y + 1, z);
    return out;
  }

  private isLogicBlock(blockId: number): boolean {
    return (
      blockId === LogicBlock.WIRE ||
      blockId === LogicBlock.TORCH_OFF ||
      blockId === LogicBlock.TORCH_ON ||
      blockId === LogicBlock.REPEATER ||
      blockId === LogicBlock.LAMP
    );
  }

  private isEmitter(blockId: number, power: number): boolean {
    return (
      (blockId === LogicBlock.TORCH_ON && power > 0) ||
      (blockId === LogicBlock.REPEATER && power > 0)
    );
  }
}
