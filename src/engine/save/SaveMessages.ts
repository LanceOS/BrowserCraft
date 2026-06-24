export interface SaveChunkMessage {
  readonly type: "SAVE_CHUNK";
  readonly chunkX: number;
  readonly chunkZ: number;
  readonly regionX: number;
  readonly regionZ: number;
  readonly rawBuffer: ArrayBuffer;
}

export interface LoadChunkMessage {
  readonly type: "LOAD_CHUNK";
  readonly chunkX: number;
  readonly chunkZ: number;
  readonly regionX: number;
  readonly regionZ: number;
}

export interface SaveCompleteMessage {
  readonly type: "SAVE_COMPLETE";
  readonly chunkX: number;
  readonly chunkZ: number;
}

export interface LoadSuccessMessage {
  readonly type: "LOAD_SUCCESS";
  readonly chunkX: number;
  readonly chunkZ: number;
  readonly buffer: ArrayBuffer;
}

export interface LoadFailedMessage {
  readonly type: "LOAD_FAILED";
  readonly chunkX: number;
  readonly chunkZ: number;
}

export interface SaveErrorMessage {
  readonly type: "SAVE_ERROR" | "LOAD_ERROR";
  readonly chunkX?: number;
  readonly chunkZ?: number;
  readonly reason: string;
}

export type SaveWorkerInboundMessage = SaveChunkMessage | LoadChunkMessage;
export type SaveWorkerOutboundMessage =
  | SaveCompleteMessage
  | LoadSuccessMessage
  | LoadFailedMessage
  | SaveErrorMessage;
