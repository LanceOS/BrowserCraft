export class Framebuffer {
  readonly framebuffer: WebGLFramebuffer;

  constructor(private readonly gl: WebGL2RenderingContext) {
    const framebuffer = gl.createFramebuffer();
    if (!framebuffer) throw new Error("Failed to create framebuffer");
    this.framebuffer = framebuffer;
  }

  dispose(): void {
    this.gl.deleteFramebuffer(this.framebuffer);
  }
}
