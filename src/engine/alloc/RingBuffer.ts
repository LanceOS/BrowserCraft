export class RingBuffer<T> {
  private readonly values: Array<T | undefined>;
  private head = 0;
  private tail = 0;
  private size = 0;

  constructor(private readonly capacity: number) {
    this.values = new Array<T | undefined>(capacity);
  }

  push(value: T): boolean {
    if (this.size === this.capacity) return false;
    this.values[this.tail] = value;
    this.tail = (this.tail + 1) % this.capacity;
    this.size++;
    return true;
  }

  shift(): T | undefined {
    if (this.size === 0) return undefined;
    const value = this.values[this.head];
    this.values[this.head] = undefined;
    this.head = (this.head + 1) % this.capacity;
    this.size--;
    return value;
  }
}
