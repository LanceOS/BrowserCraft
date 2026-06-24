export type ChunkState =
  | "loadingFromDisk"
  | "queuedGen"
  | "generating"
  | "voxelsReady"
  | "queuedMesh"
  | "meshing"
  | "meshReady"
  | "uploaded"
  | "meshFailed";

export class Chunk {
  state: ChunkState = "queuedGen";
  vertexCount = 0;
  indexCount = 0;
  needsRemesh = false;

  constructor(
    readonly chunkX: number,
    readonly chunkZ: number,
    readonly slotIndex: number,
  ) {}

  get key(): string {
    return `${this.chunkX}:${this.chunkZ}`;
  }
}
