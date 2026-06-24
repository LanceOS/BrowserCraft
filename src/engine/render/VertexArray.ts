export class VertexArray {
  readonly vao: WebGLVertexArrayObject;

  constructor(private readonly gl: WebGL2RenderingContext) {
    const vao = gl.createVertexArray();
    if (!vao) throw new Error("Failed to create VAO");
    this.vao = vao;
  }

  bind(): void {
    this.gl.bindVertexArray(this.vao);
  }

  dispose(): void {
    this.gl.deleteVertexArray(this.vao);
  }
}
