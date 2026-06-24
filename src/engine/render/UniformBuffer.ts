export class UniformBuffer {
  readonly buffer: WebGLBuffer;

  constructor(
    private readonly gl: WebGL2RenderingContext,
    readonly binding: number,
    readonly byteSize: number,
  ) {
    const buffer = this.gl.createBuffer();
    if (!buffer) throw new Error("Failed to create uniform buffer");
    this.buffer = buffer;
    this.gl.bindBuffer(this.gl.UNIFORM_BUFFER, this.buffer);
    this.gl.bufferData(this.gl.UNIFORM_BUFFER, byteSize, this.gl.DYNAMIC_DRAW);
    this.gl.bindBufferBase(this.gl.UNIFORM_BUFFER, binding, this.buffer);
  }

  upload(data: ArrayBufferView): void {
    this.gl.bindBuffer(this.gl.UNIFORM_BUFFER, this.buffer);
    this.gl.bufferSubData(this.gl.UNIFORM_BUFFER, 0, data);
    this.gl.bindBufferBase(this.gl.UNIFORM_BUFFER, this.binding, this.buffer);
  }

  dispose(): void {
    this.gl.deleteBuffer(this.buffer);
  }
}
