export interface GameConfig {
  renderDistance: number;
  chunkSize: number;
  worldHeight: number;
  maxConcurrentGenJobs: number;
  maxConcurrentMeshJobs: number;
  textureArrayLayers: number;
  targetFps: number;
  fovDegrees: number;
  worldSeed: number;
  maxVertsPerChunk: number;
  maxIndicesPerChunk: number;
}

export const DefaultConfig: GameConfig = {
  renderDistance: 2,
  chunkSize: 16,
  worldHeight: 256,
  maxConcurrentGenJobs: 2,
  maxConcurrentMeshJobs: 2,
  textureArrayLayers: 64,
  targetFps: 60,
  fovDegrees: 70,
  worldSeed: 1337,
  maxVertsPerChunk: 24576,
  maxIndicesPerChunk: 36864,
};
