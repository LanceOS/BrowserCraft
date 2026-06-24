export const enum ChunkSlotStatus {
  FREE = 0,
  GENERATING = 1,
  VOXELS_READY = 2,
  MESHING = 3,
  MESH_READY = 4,
  GPU_UPLOADED = 5,
}

export interface ChunkDimensions {
  readonly sizeX: number;
  readonly sizeY: number;
  readonly sizeZ: number;
  readonly maxVertsPerChunk: number;
  readonly maxIndicesPerChunk: number;
  readonly vertexStrideFloats: number;
}

export interface ChunkSlot {
  readonly slotIndex: number;
  readonly buffer: SharedArrayBuffer;
  readonly baseByteOffset: number;
  readonly status: Int32Array;
  readonly vertexCount: Uint32Array;
  readonly indexCount: Uint32Array;
  readonly chunkX: Int32Array;
  readonly chunkZ: Int32Array;
  readonly genSeed: Uint32Array;
  readonly voxels: Uint8Array;
  readonly light: Uint8Array;
  readonly redstone: Uint8Array;
  readonly vertices: Float32Array;
  readonly indices: Uint32Array;
}

export interface SharedPoolBootstrap {
  readonly buffer: SharedArrayBuffer;
  readonly capacity: number;
  readonly dims: ChunkDimensions;
}

export interface ChunkJobMessage {
  readonly slotIndex: number;
  readonly chunkX: number;
  readonly chunkZ: number;
  readonly seed: number;
}

export class SharedPool {
  private readonly rootBuffer: SharedArrayBuffer;
  private readonly slotByteSize: number;
  private readonly headerBytes = 32;
  private readonly voxelsBytes: number;
  private readonly lightBytes: number;
  private readonly redstoneBytes: number;
  private readonly vertsBytes: number;
  private readonly indicesBytes: number;
  private readonly freeList: Uint32Array | null;
  private freeHead: number;

  private constructor(
    private readonly capacityValue: number,
    private readonly dimsValue: ChunkDimensions,
    buffer?: SharedArrayBuffer,
  ) {
    this.voxelsBytes = dimsValue.sizeX * dimsValue.sizeY * dimsValue.sizeZ;
    this.lightBytes = this.voxelsBytes;
    this.redstoneBytes = this.voxelsBytes;
    this.vertsBytes = dimsValue.maxVertsPerChunk * dimsValue.vertexStrideFloats * 4;
    this.indicesBytes = dimsValue.maxIndicesPerChunk * 4;

    const unaligned =
      this.headerBytes +
      this.voxelsBytes +
      this.lightBytes +
      this.redstoneBytes +
      this.vertsBytes +
      this.indicesBytes;
    this.slotByteSize = (unaligned + 63) & ~63;
    this.rootBuffer = buffer ?? new SharedArrayBuffer(this.slotByteSize * capacityValue);

    if (buffer) {
      this.freeList = null;
      this.freeHead = 0;
    } else {
      this.freeList = new Uint32Array(capacityValue);
      for (let i = 0; i < capacityValue; i++) this.freeList[i] = i;
      this.freeHead = capacityValue;
    }
  }

  static create(capacity: number, dims: ChunkDimensions): SharedPool {
    return new SharedPool(capacity, dims);
  }

  static attach(buffer: SharedArrayBuffer, capacity: number, dims: ChunkDimensions): SharedPool {
    return new SharedPool(capacity, dims, buffer);
  }

  get rawBuffer(): SharedArrayBuffer {
    return this.rootBuffer;
  }

  get slotSize(): number {
    return this.slotByteSize;
  }

  get dimensions(): ChunkDimensions {
    return this.dimsValue;
  }

  get capacity(): number {
    return this.capacityValue;
  }

  bootstrap(): SharedPoolBootstrap {
    return {
      buffer: this.rootBuffer,
      capacity: this.capacityValue,
      dims: this.dimsValue,
    };
  }

  acquire(): ChunkSlot | null {
    if (!this.freeList) throw new Error("Cannot acquire slots from an attached worker view");
    if (this.freeHead === 0) return null;
    const slotIndex = this.freeList[--this.freeHead];
    const slot = this.view(slotIndex);
    Atomics.store(slot.status, 0, ChunkSlotStatus.FREE);
    slot.vertexCount[0] = 0;
    slot.indexCount[0] = 0;
    slot.voxels.fill(0);
    slot.light.fill(0);
    slot.redstone.fill(0);
    return slot;
  }

  release(slot: ChunkSlot): void {
    if (!this.freeList) throw new Error("Cannot release slots from an attached worker view");
    Atomics.store(slot.status, 0, ChunkSlotStatus.FREE);
    this.freeList[this.freeHead++] = slot.slotIndex;
  }

  view(slotIndex: number): ChunkSlot {
    const base = slotIndex * this.slotByteSize;
    const buffer = this.rootBuffer;
    return {
      slotIndex,
      buffer,
      baseByteOffset: base,
      status: new Int32Array(buffer, base + 0, 1),
      vertexCount: new Uint32Array(buffer, base + 4, 1),
      indexCount: new Uint32Array(buffer, base + 8, 1),
      chunkX: new Int32Array(buffer, base + 12, 1),
      chunkZ: new Int32Array(buffer, base + 16, 1),
      genSeed: new Uint32Array(buffer, base + 20, 1),
      voxels: new Uint8Array(buffer, base + this.headerBytes, this.voxelsBytes),
      light: new Uint8Array(buffer, base + this.headerBytes + this.voxelsBytes, this.lightBytes),
      redstone: new Uint8Array(
        buffer,
        base + this.headerBytes + this.voxelsBytes + this.lightBytes,
        this.redstoneBytes,
      ),
      vertices: new Float32Array(
        buffer,
        base + this.headerBytes + this.voxelsBytes + this.lightBytes + this.redstoneBytes,
        this.dimsValue.maxVertsPerChunk * this.dimsValue.vertexStrideFloats,
      ),
      indices: new Uint32Array(
        buffer,
        base + this.headerBytes + this.voxelsBytes + this.lightBytes + this.redstoneBytes + this.vertsBytes,
        this.dimsValue.maxIndicesPerChunk,
      ),
    };
  }
}
