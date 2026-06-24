export class VertexBuffer {
  readonly buffer: WebGLBuffer;

  constructor(
    private readonly gl: WebGL2RenderingContext,
    private readonly target: number,
  ) {
    const buffer = gl.createBuffer();
    if (!buffer) throw new Error("Failed to create buffer");
    this.buffer = buffer;
  }

  upload(data: BufferSource, usage: number): void {
    this.gl.bindBuffer(this.target, this.buffer);
    this.gl.bufferData(this.target, data, usage);
  }

  dispose(): void {
    this.gl.deleteBuffer(this.buffer);
  }
}
