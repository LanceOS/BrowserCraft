/// <reference lib="webworker" />

import { ChunkSlotStatus, SharedPool } from "../../alloc/SharedPool.js";
import type { WorkerInboundMessage, WorkerOutboundMessage, WorkerInitMessage, WorldGenJobMessage } from "../messages.js";
import { BiomeId, BiomeSampler } from "./BiomeSampler.js";
import { CaveCarver } from "./CaveCarver.js";
import { OreDistributor } from "./OreDistributor.js";
import { SimplexNoise } from "./SimplexNoise.js";
import { StructureFactory } from "../../../content/structures/StructureRegistry.js";
import { VillagePlanner } from "../../../content/structures/VillagePlanner.js";

const ctx = self as unknown as DedicatedWorkerGlobalScope;

let pool: SharedPool | null = null;
let pipeline: WorldGenPipeline | null = null;

export class WorldGenPipeline {
  private readonly densityNoise: SimplexNoise;
  private readonly biomeSampler: BiomeSampler;
  private readonly caveCarver: CaveCarver;
  private readonly oreDist: OreDistributor;
  private readonly villagePlanner: VillagePlanner;

  private static readonly STONE = 1;
  private static readonly DIRT = 3;
  private static readonly BEDROCK = 7;
  private static readonly WATER = 8;

  constructor(seed: number) {
    this.densityNoise = new SimplexNoise(seed);
    this.biomeSampler = new BiomeSampler(seed);
    this.caveCarver = new CaveCarver(seed);
    this.oreDist = new OreDistributor(seed);
    this.villagePlanner = new VillagePlanner(new StructureFactory().getAll());
  }

  generate(slotIndex: number): void {
    if (!pool) return;
    const slot = pool.view(slotIndex);
    const voxels = slot.voxels;
    const { sizeX, sizeY, sizeZ } = pool.dimensions;
    const chunkX = Atomics.load(slot.chunkX, 0);
    const chunkZ = Atomics.load(slot.chunkZ, 0);
    const baseX = chunkX * sizeX;
    const baseZ = chunkZ * sizeZ;

    for (let z = 0; z < sizeZ; z++) {
      for (let x = 0; x < sizeX; x++) {
        const worldX = baseX + x;
        const worldZ = baseZ + z;
        const heightMap = this.biomeSampler.noise2D(worldX * 0.01, worldZ * 0.01);
        const baseHeight = Math.floor(64 + heightMap * 16);
        const biome = this.biomeSampler.sampleBiome(worldX, worldZ);
        const rule = this.biomeSampler.getRule(biome);

        for (let y = 0; y < sizeY; y++) {
          const index = (y * sizeZ + z) * sizeX + x;

          if (y === 0) {
            voxels[index] = WorldGenPipeline.BEDROCK;
            continue;
          }

          if (y > baseHeight) {
            voxels[index] = y <= 64 && biome !== BiomeId.DESERT ? WorldGenPipeline.WATER : 0;
            continue;
          }

          const depthFactor = (baseHeight - y) * 0.05;
          const noise3D = this.densityNoise.noise3D(worldX * 0.03, y * 0.03, worldZ * 0.03);
          if (noise3D + depthFactor < 0 && y < baseHeight - 5) {
            voxels[index] = 0;
            continue;
          }

          if (y === baseHeight) {
            voxels[index] = rule.topBlock;
          } else if (y > baseHeight - rule.depth) {
            voxels[index] = rule.fillerBlock;
          } else {
            voxels[index] = WorldGenPipeline.STONE;
          }
        }
      }
    }

    this.caveCarver.carve(voxels, baseX, baseZ, sizeX, sizeY, sizeZ);
    const regionSize = 64;
    const baseRegionX = Math.floor((chunkX * sizeX) / regionSize);
    const baseRegionZ = Math.floor((chunkZ * sizeZ) / regionSize);
    for (let regionX = baseRegionX - 1; regionX <= baseRegionX + 1; regionX++) {
      for (let regionZ = baseRegionZ - 1; regionZ <= baseRegionZ + 1; regionZ++) {
        this.villagePlanner.generate(regionX, regionZ, chunkX, chunkZ, voxels, { sizeX, sizeY, sizeZ });
      }
    }
    this.oreDist.distribute(voxels, sizeX, sizeY, sizeZ);
    Atomics.store(slot.status, 0, ChunkSlotStatus.VOXELS_READY);
  }
}

const fillChunk = (slotIndex: number, chunkX: number, chunkZ: number, seed: number): void => {
  if (!pool || !pipeline) return;

  const slot = pool.view(slotIndex);
  if (Atomics.load(slot.status, 0) !== ChunkSlotStatus.GENERATING) return;

  Atomics.store(slot.chunkX, 0, chunkX);
  Atomics.store(slot.chunkZ, 0, chunkZ);
  slot.genSeed[0] = seed >>> 0;
  pipeline.generate(slotIndex);
  const message: WorkerOutboundMessage = {
    kind: "generated",
    slotIndex,
    chunkX,
    chunkZ,
  };
  ctx.postMessage(message);
};

ctx.onmessage = (event: MessageEvent<WorkerInboundMessage>) => {
  const msg = event.data;
  if (msg.kind === "init") {
    const init = msg as WorkerInitMessage;
    pool = SharedPool.attach(init.pool.buffer, init.pool.capacity, init.pool.dims);
    pipeline = new WorldGenPipeline(init.seed);
    return;
  }

  if (msg.kind === "generate") {
    const job = msg as WorldGenJobMessage;
    fillChunk(job.slotIndex, job.chunkX, job.chunkZ, job.seed);
  }
};
