import type { SharedPoolBootstrap } from "../alloc/SharedPool.js";
import type { WorkerInitMessage, WorkerKind } from "./messages.js";

export interface WorkerHandle {
  readonly id: number;
  readonly kind: WorkerKind;
  readonly worker: Worker;
  busy: boolean;
}

export const spawnWorkers = (
  kind: WorkerKind,
  count: number,
  pool: SharedPoolBootstrap,
  seed: number,
): WorkerHandle[] => {
  const url =
    kind === "worldgen"
      ? new URL("./worldgen/WorldGenWorker.js", import.meta.url)
      : new URL("./mesher/MesherWorker.js", import.meta.url);

  const initMessage: WorkerInitMessage = {
    kind: "init",
    pool,
    seed,
  };

  return Array.from({ length: count }, (_, id) => {
    const worker = new Worker(url, { type: "module" });
    worker.postMessage(initMessage);
    return { id, kind, worker, busy: false };
  });
};
