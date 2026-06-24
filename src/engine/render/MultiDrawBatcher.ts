import { ScratchArena } from "../alloc/ScratchArena.js";

export interface ChunkGpuHandle {
  readonly vboOffset: number;
  readonly iboOffset: number;
  readonly indexCount: number;
}

type MultiDrawExtension = {
  multiDrawElementsWEBGL(
    mode: number,
    countsList: Int32Array,
    countsOffset: number,
    type: number,
    offsetsList: Int32Array,
    offsetsOffset: number,
    drawcount: number,
  ): void;
};

export class MultiDrawBatcher {
  private readonly ext: MultiDrawExtension | null;
  private readonly masterVbo: WebGLBuffer;
  private readonly masterIbo: WebGLBuffer;
  private readonly masterVao: WebGLVertexArrayObject;
  private readonly arena = new ScratchArena(1 << 16);

  constructor(
    private readonly gl: WebGL2RenderingContext,
    maxVertices: number,
    maxIndices: number,
    readonly vertexStrideBytes: number,
  ) {
    this.ext = gl.getExtension("WEBGL_multi_draw") as MultiDrawExtension | null;
    const vao = gl.createVertexArray();
    const vbo = gl.createBuffer();
    const ibo = gl.createBuffer();
    if (!vao || !vbo || !ibo) throw new Error("Failed to initialize multi-draw batcher");

    this.masterVao = vao;
    this.masterVbo = vbo;
    this.masterIbo = ibo;

    gl.bindVertexArray(this.masterVao);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.masterVbo);
    gl.bufferData(gl.ARRAY_BUFFER, maxVertices * this.vertexStrideBytes, gl.DYNAMIC_DRAW);
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.masterIbo);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, maxIndices * 4, gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, this.vertexStrideBytes, 0);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, this.vertexStrideBytes, 12);
    gl.enableVertexAttribArray(2);
    gl.vertexAttribPointer(2, 2, gl.FLOAT, false, this.vertexStrideBytes, 24);
    gl.enableVertexAttribArray(3);
    gl.vertexAttribPointer(3, 1, gl.FLOAT, false, this.vertexStrideBytes, 32);
    gl.enableVertexAttribArray(4);
    gl.vertexAttribPointer(4, 1, gl.FLOAT, false, this.vertexStrideBytes, 36);
    gl.bindVertexArray(null);
  }

  uploadChunkMesh(slotIndex: number, vertices: Float32Array, indices: Uint32Array): ChunkGpuHandle {
    const vboByteOffset = slotIndex * 1024 * 1024;
    const iboByteOffset = slotIndex * 256 * 1024;

    this.gl.bindBuffer(this.gl.ARRAY_BUFFER, this.masterVbo);
    this.gl.bufferSubData(this.gl.ARRAY_BUFFER, vboByteOffset, vertices);
    this.gl.bindBuffer(this.gl.ELEMENT_ARRAY_BUFFER, this.masterIbo);
    this.gl.bufferSubData(this.gl.ELEMENT_ARRAY_BUFFER, iboByteOffset, indices);

    return { vboOffset: vboByteOffset, iboOffset: iboByteOffset, indexCount: indices.length };
  }

  drawVisible(visibleHandles: readonly ChunkGpuHandle[]): void {
    if (visibleHandles.length === 0) return;
    this.arena.reset();

    const counts = this.arena.alloc(Int32Array, visibleHandles.length);
    const offsets = this.arena.alloc(Int32Array, visibleHandles.length);

    for (let i = 0; i < visibleHandles.length; i++) {
      counts[i] = visibleHandles[i].indexCount;
      offsets[i] = visibleHandles[i].iboOffset;
    }

    this.gl.bindVertexArray(this.masterVao);
    if (this.ext) {
      this.ext.multiDrawElementsWEBGL(
        this.gl.TRIANGLES,
        counts,
        0,
        this.gl.UNSIGNED_INT,
        offsets,
        0,
        visibleHandles.length,
      );
    } else {
      for (let i = 0; i < visibleHandles.length; i++) {
        this.gl.drawElements(this.gl.TRIANGLES, counts[i], this.gl.UNSIGNED_INT, offsets[i]);
      }
    }
    this.gl.bindVertexArray(null);
  }
}
