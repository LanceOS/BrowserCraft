export class Texture2DArray {
  readonly texture: WebGLTexture;

  constructor(
    private readonly gl: WebGL2RenderingContext,
    width: number,
    height: number,
    layers: number,
  ) {
    const texture = gl.createTexture();
    if (!texture) throw new Error("Failed to create texture array");
    this.texture = texture;
    const levels = Math.floor(Math.log2(Math.max(width, height))) + 1;

    gl.bindTexture(gl.TEXTURE_2D_ARRAY, texture);
    gl.texStorage3D(gl.TEXTURE_2D_ARRAY, levels, gl.RGBA8, width, height, layers);
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MIN_FILTER, gl.NEAREST_MIPMAP_LINEAR);
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_WRAP_S, gl.REPEAT);
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_WRAP_T, gl.REPEAT);
  }

  uploadLayer(layer: number, source: Uint8Array, width: number, height: number): void {
    this.gl.bindTexture(this.gl.TEXTURE_2D_ARRAY, this.texture);
    this.gl.texSubImage3D(
      this.gl.TEXTURE_2D_ARRAY,
      0,
      0,
      0,
      layer,
      width,
      height,
      1,
      this.gl.RGBA,
      this.gl.UNSIGNED_BYTE,
      source,
    );
  }

  generateMipmaps(): void {
    this.gl.bindTexture(this.gl.TEXTURE_2D_ARRAY, this.texture);
    this.gl.generateMipmap(this.gl.TEXTURE_2D_ARRAY);
  }

  bind(unit: number): void {
    this.gl.activeTexture(this.gl.TEXTURE0 + unit);
    this.gl.bindTexture(this.gl.TEXTURE_2D_ARRAY, this.texture);
  }

  dispose(): void {
    this.gl.deleteTexture(this.texture);
  }
}
