const INDEX_MASK = 0x00ff_ffff;
const GEN_SHIFT = 24;

export class EntityManager {
  private readonly generation: Uint8Array;
  private readonly freeIndices: Int32Array;
  private freeHead = 0;
  private liveCount = 0;

  constructor(readonly capacity: number = 1 << 18) {
    this.generation = new Uint8Array(capacity);
    this.freeIndices = new Int32Array(capacity);
    for (let i = capacity - 1; i >= 0; i--) this.freeIndices[this.freeHead++] = i;
  }

  allocate(): number {
    if (this.freeHead === 0) throw new Error("EntityManager capacity exhausted");
    const idx = this.freeIndices[--this.freeHead];
    const gen = this.generation[idx];
    this.liveCount++;
    return (gen << GEN_SHIFT) | idx;
  }

  destroy(id: number): void {
    const idx = id & INDEX_MASK;
    const gen = id >>> GEN_SHIFT;
    if (gen !== this.generation[idx]) return;
    this.generation[idx] = (gen + 1) & 0xff;
    this.freeIndices[this.freeHead++] = idx;
    this.liveCount--;
  }

  isAlive(id: number): boolean {
    const idx = id & INDEX_MASK;
    return (id >>> GEN_SHIFT) === this.generation[idx];
  }

  static indexOf(id: number): number {
    return id & INDEX_MASK;
  }

  get count(): number {
    return this.liveCount;
  }
}
