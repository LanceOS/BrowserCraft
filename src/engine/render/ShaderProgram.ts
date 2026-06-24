export class ShaderProgram {
  readonly program: WebGLProgram;
  private readonly uniforms = new Map<string, WebGLUniformLocation | null>();

  constructor(
    private readonly gl: WebGL2RenderingContext,
    vertexSource: string,
    fragmentSource: string,
  ) {
    const vertexShader = this.compile(gl.VERTEX_SHADER, vertexSource);
    const fragmentShader = this.compile(gl.FRAGMENT_SHADER, fragmentSource);

    const program = gl.createProgram();
    if (!program) throw new Error("Failed to create shader program");
    gl.attachShader(program, vertexShader);
    gl.attachShader(program, fragmentShader);
    gl.linkProgram(program);

    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
      const log = gl.getProgramInfoLog(program) ?? "Unknown link error";
      gl.deleteShader(vertexShader);
      gl.deleteShader(fragmentShader);
      gl.deleteProgram(program);
      throw new Error(`Shader link failed: ${log}`);
    }

    gl.deleteShader(vertexShader);
    gl.deleteShader(fragmentShader);
    this.program = program;
  }

  use(): void {
    this.gl.useProgram(this.program);
  }

  bindUniformBlock(blockName: string, binding: number): void {
    const index = this.gl.getUniformBlockIndex(this.program, blockName);
    if (index === this.gl.INVALID_INDEX) return;
    this.gl.uniformBlockBinding(this.program, index, binding);
  }

  uniform(name: string): WebGLUniformLocation | null {
    if (this.uniforms.has(name)) return this.uniforms.get(name) ?? null;
    const location = this.gl.getUniformLocation(this.program, name);
    this.uniforms.set(name, location);
    return location;
  }

  dispose(): void {
    this.gl.deleteProgram(this.program);
  }

  private compile(type: number, source: string): WebGLShader {
    const shader = this.gl.createShader(type);
    if (!shader) throw new Error("Failed to create shader");
    this.gl.shaderSource(shader, source);
    this.gl.compileShader(shader);
    if (!this.gl.getShaderParameter(shader, this.gl.COMPILE_STATUS)) {
      const log = this.gl.getShaderInfoLog(shader) ?? "Unknown compile error";
      this.gl.deleteShader(shader);
      throw new Error(`Shader compile failed: ${log}`);
    }
    return shader;
  }
}
