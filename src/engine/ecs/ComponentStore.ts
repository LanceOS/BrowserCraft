type TypedArray =
  | Float32Array
  | Float64Array
  | Int16Array
  | Int32Array
  | Uint8Array
  | Uint32Array;

type TypedArrayCtor =
  | Float32ArrayConstructor
  | Float64ArrayConstructor
  | Int16ArrayConstructor
  | Int32ArrayConstructor
  | Uint8ArrayConstructor
  | Uint32ArrayConstructor;

export interface ComponentFieldDesc {
  readonly type: TypedArrayCtor;
  readonly length: number;
}

export type ComponentDesc = Readonly<Record<string, ComponentFieldDesc>>;

type FieldType<D extends ComponentDesc, K extends keyof D> =
  D[K]["type"] extends Float32ArrayConstructor ? Float32Array :
  D[K]["type"] extends Float64ArrayConstructor ? Float64Array :
  D[K]["type"] extends Int16ArrayConstructor ? Int16Array :
  D[K]["type"] extends Int32ArrayConstructor ? Int32Array :
  D[K]["type"] extends Uint8ArrayConstructor ? Uint8Array :
  D[K]["type"] extends Uint32ArrayConstructor ? Uint32Array :
  never;

export type ComponentSoA<D extends ComponentDesc> = {
  readonly [K in keyof D]: FieldType<D, K>;
};

export class ComponentStore<D extends ComponentDesc> {
  readonly capacity: number;
  readonly data: ComponentSoA<D>;
  private denseCount = 0;
  private readonly sparse: Int32Array;
  private readonly dense: Int32Array;
  private readonly fieldLengths: Record<string, number>;

  constructor(desc: D, capacity: number) {
    this.capacity = capacity;
    this.sparse = new Int32Array(capacity).fill(-1);
    this.dense = new Int32Array(capacity);
    this.fieldLengths = {};
    const data = {} as Record<string, TypedArray>;

    for (const key of Object.keys(desc)) {
      const field = desc[key];
      data[key] = new field.type(field.length * capacity);
      this.fieldLengths[key] = field.length;
    }

    this.data = data as ComponentSoA<D>;
  }

  rowFor(entityIndex: number): number {
    return this.sparse[entityIndex];
  }

  add(entityIndex: number): number {
    const existing = this.sparse[entityIndex];
    if (existing !== -1) return existing;
    const row = this.denseCount++;
    this.sparse[entityIndex] = row;
    this.dense[row] = entityIndex;
    return row;
  }

  remove(entityIndex: number): void {
    const row = this.sparse[entityIndex];
    if (row === -1) return;
    const last = --this.denseCount;

    if (row !== last) {
      for (const key of Object.keys(this.data)) {
        const arr = this.data[key as keyof D] as unknown as TypedArray;
        const width = this.fieldLengths[key];
        for (let i = 0; i < width; i++) {
          arr[row * width + i] = arr[last * width + i];
        }
      }

      const movedEntity = this.dense[last];
      this.dense[row] = movedEntity;
      this.sparse[movedEntity] = row;
    }

    this.sparse[entityIndex] = -1;
  }

  *rows(): IterableIterator<number> {
    for (let i = 0; i < this.denseCount; i++) yield i;
  }

  entityAtRow(row: number): number {
    return this.dense[row];
  }

  get count(): number {
    return this.denseCount;
  }
}
