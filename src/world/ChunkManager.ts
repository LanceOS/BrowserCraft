import { Chunk } from "./Chunk.js";

export const packChunkKey = (chunkX: number, chunkZ: number): string => `${chunkX}:${chunkZ}`;

export class ChunkManager {
  private readonly chunks = new Map<string, Chunk>();

  get(chunkX: number, chunkZ: number): Chunk | undefined {
    return this.chunks.get(packChunkKey(chunkX, chunkZ));
  }

  set(chunk: Chunk): void {
    this.chunks.set(chunk.key, chunk);
  }

  has(chunkX: number, chunkZ: number): boolean {
    return this.chunks.has(packChunkKey(chunkX, chunkZ));
  }

  delete(chunkX: number, chunkZ: number): boolean {
    return this.chunks.delete(packChunkKey(chunkX, chunkZ));
  }

  values(): IterableIterator<Chunk> {
    return this.chunks.values();
  }

  keys(): IterableIterator<string> {
    return this.chunks.keys();
  }

  hasKey(key: string): boolean {
    return this.chunks.has(key);
  }
}
