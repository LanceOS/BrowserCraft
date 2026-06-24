export class RleChunkSection {
  private readonly layers: Array<Uint8Array | null> = new Array(16).fill(null);

  get(x: number, y: number, z: number): number {
    const layer = this.layers[y];
    if (layer === null) return 0;
    if (layer.length === 1) return layer[0];
    return layer[z * 16 + x];
  }

  set(x: number, y: number, z: number, blockId: number): void {
    let layer = this.layers[y];
    if (layer === null) {
      this.layers[y] = new Uint8Array([blockId]);
      return;
    }

    if (layer.length === 1) {
      if (layer[0] === blockId) return;
      const expanded = new Uint8Array(256).fill(layer[0]);
      expanded[z * 16 + x] = blockId;
      this.layers[y] = expanded;
      return;
    }

    layer[z * 16 + x] = blockId;
  }
}
