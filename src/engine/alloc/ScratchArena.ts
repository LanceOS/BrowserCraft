type TypedArray =
  | Float32Array
  | Int32Array
  | Uint32Array
  | Uint8Array;

interface TypedArrayConstructor<T extends TypedArray> {
  readonly BYTES_PER_ELEMENT: number;
  new (buffer: ArrayBuffer, byteOffset: number, length: number): T;
}

export class ScratchArena {
  private readonly buffer: ArrayBuffer;
  private offset = 0;

  constructor(readonly byteSize: number = 1 << 20) {
    this.buffer = new ArrayBuffer(byteSize);
  }

  alloc<T extends TypedArray>(type: TypedArrayConstructor<T>, count: number): T {
    const bytes = count * type.BYTES_PER_ELEMENT;
    this.offset = (this.offset + 3) & ~3;
    if (this.offset + bytes > this.byteSize) {
      throw new Error(`ScratchArena overflow: requested ${bytes}, available ${this.byteSize - this.offset}`);
    }

    const start = this.offset;
    this.offset += bytes;
    return new type(this.buffer, start, count);
  }

  reset(): void {
    this.offset = 0;
  }
}
