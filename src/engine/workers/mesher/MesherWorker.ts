/// <reference lib="webworker" />

import { ChunkSlotStatus, SharedPool } from "../../alloc/SharedPool.js";
import { BlockRegistry } from "../../../world/BlockRegistry.js";
import { VanillaBlockFactory } from "../../../world/BlockFactory.js";
import type { MeshDoneMessage, MeshJobMessage, WorkerInboundMessage, WorkerInitMessage } from "../messages.js";
import { greedyMeshChunk } from "./GreedyMesher.js";
import { LightPropagator } from "./LightPropagator.js";

const ctx = self as unknown as DedicatedWorkerGlobalScope;

let pool: SharedPool | null = null;
let blocks: BlockRegistry | null = null;
let lightEmissionMap: Map<number, number> | null = null;
let transparentBlocks: Set<number> | null = null;

ctx.onmessage = (event: MessageEvent<WorkerInboundMessage>) => {
  const msg = event.data;
  if (msg.kind === "init") {
    const init = msg as WorkerInitMessage;
    pool = SharedPool.attach(init.pool.buffer, init.pool.capacity, init.pool.dims);
    blocks = new BlockRegistry(4096);
    new VanillaBlockFactory().registerAll(blocks);
    lightEmissionMap = new Map();
    transparentBlocks = new Set<number>([0]);
    for (const def of blocks.values()) {
      if (def.material.transparent || def.material.foliage || def.material.liquid) {
        transparentBlocks.add(def.id);
      }
      if (def.material.lightEmission > 0) {
        lightEmissionMap.set(def.id, def.material.lightEmission);
      }
    }
    return;
  }

  if (msg.kind !== "mesh" || !pool || !blocks || !lightEmissionMap || !transparentBlocks) return;
  const job = msg as MeshJobMessage;
  const slot = pool.view(job.slotIndex);
  if (Atomics.load(slot.status, 0) !== ChunkSlotStatus.MESHING) return;
  const emissionMap = lightEmissionMap;

  LightPropagator.calculate(
    slot.voxels,
    slot.light,
    pool.dimensions,
    (blockId, index) => {
      if (blockId === 123 && (slot.redstone[index] & 0x0f) > 0) return 15;
      return emissionMap.get(blockId) ?? 0;
    },
    transparentBlocks,
  );
  const success = greedyMeshChunk(slot, pool.dimensions, blocks);
  Atomics.store(slot.status, 0, ChunkSlotStatus.MESH_READY);

  const message: MeshDoneMessage = {
    kind: "meshed",
    slotIndex: job.slotIndex,
    success,
    vertexCount: slot.vertexCount[0],
    indexCount: slot.indexCount[0],
  };
  ctx.postMessage(message);
};
