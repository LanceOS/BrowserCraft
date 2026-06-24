import type { ChunkJobMessage, SharedPoolBootstrap } from "../alloc/SharedPool.js";

export type WorkerKind = "worldgen" | "mesher";

export interface WorkerInitMessage {
  readonly kind: "init";
  readonly pool: SharedPoolBootstrap;
  readonly seed: number;
}

export interface WorldGenJobMessage extends ChunkJobMessage {
  readonly kind: "generate";
}

export interface MeshJobMessage {
  readonly kind: "mesh";
  readonly slotIndex: number;
  readonly neighborSlotIndices?: {
    readonly negX?: number;
    readonly posX?: number;
    readonly negZ?: number;
    readonly posZ?: number;
  };
}

export interface WorldGenDoneMessage {
  readonly kind: "generated";
  readonly slotIndex: number;
  readonly chunkX: number;
  readonly chunkZ: number;
}

export interface MeshDoneMessage {
  readonly kind: "meshed";
  readonly slotIndex: number;
  readonly success: boolean;
  readonly vertexCount: number;
  readonly indexCount: number;
}

export type WorkerInboundMessage = WorkerInitMessage | WorldGenJobMessage | MeshJobMessage;
export type WorkerOutboundMessage = WorldGenDoneMessage | MeshDoneMessage;
