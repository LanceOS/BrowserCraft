export class TypedArrayPool<TArray extends ArrayBufferView> {
  private readonly pool: TArray[] = [];

  constructor(private readonly factory: () => TArray) {}

  acquire(): TArray {
    return this.pool.pop() ?? this.factory();
  }

  release(value: TArray): void {
    this.pool.push(value);
  }
}
