export class WorkerCompletionQueue {
  private readonly sharedMem: SharedArrayBuffer;
  private readonly writeIndex: Int32Array;
  private readonly readIndex: Int32Array;
  private readonly data: Int32Array;
  private readonly capacity: number;

  constructor(capacity: number) {
    this.capacity = capacity;
    this.sharedMem = new SharedArrayBuffer(8 + capacity * 4);
    this.writeIndex = new Int32Array(this.sharedMem, 0, 1);
    this.readIndex = new Int32Array(this.sharedMem, 4, 1);
    this.data = new Int32Array(this.sharedMem, 8, capacity);
  }

  get buffer(): SharedArrayBuffer {
    return this.sharedMem;
  }

  push(slotIndex: number): boolean {
    const write = Atomics.load(this.writeIndex, 0);
    const read = Atomics.load(this.readIndex, 0);
    if (write - read >= this.capacity) return false;
    this.data[write % this.capacity] = slotIndex;
    Atomics.store(this.writeIndex, 0, write + 1);
    return true;
  }

  poll(): number | null {
    const read = Atomics.load(this.readIndex, 0);
    const write = Atomics.load(this.writeIndex, 0);
    if (read === write) return null;
    const slotIndex = this.data[read % this.capacity];
    Atomics.store(this.readIndex, 0, read + 1);
    return slotIndex;
  }
}
