import type { Chunk } from "../../world/Chunk.js";
import { VertexArray } from "./VertexArray.js";
import { VertexBuffer } from "./VertexBuffer.js";

export class ChunkMesh {
  readonly vao: VertexArray;
  readonly vertices: VertexBuffer;
  readonly indices: VertexBuffer;
  indexCount = 0;

  constructor(private readonly gl: WebGL2RenderingContext) {
    this.vao = new VertexArray(gl);
    this.vertices = new VertexBuffer(gl, gl.ARRAY_BUFFER);
    this.indices = new VertexBuffer(gl, gl.ELEMENT_ARRAY_BUFFER);
  }

  upload(
    chunk: Chunk,
    vertexData: Float32Array,
    indexData: Uint32Array,
    vertexStrideFloats: number,
  ): void {
    const strideBytes = vertexStrideFloats * 4;
    this.vao.bind();
    this.vertices.upload(vertexData, this.gl.STATIC_DRAW);
    this.indices.upload(indexData, this.gl.STATIC_DRAW);

    this.gl.enableVertexAttribArray(0);
    this.gl.vertexAttribPointer(0, 3, this.gl.FLOAT, false, strideBytes, 0);
    this.gl.enableVertexAttribArray(1);
    this.gl.vertexAttribPointer(1, 3, this.gl.FLOAT, false, strideBytes, 12);
    this.gl.enableVertexAttribArray(2);
    this.gl.vertexAttribPointer(2, 2, this.gl.FLOAT, false, strideBytes, 24);
    this.gl.enableVertexAttribArray(3);
    this.gl.vertexAttribPointer(3, 1, this.gl.FLOAT, false, strideBytes, 32);
    this.gl.enableVertexAttribArray(4);
    this.gl.vertexAttribPointer(4, 1, this.gl.FLOAT, false, strideBytes, 36);

    this.indexCount = indexData.length;
    void chunk;
  }

  draw(): void {
    this.vao.bind();
    this.gl.drawElements(this.gl.TRIANGLES, this.indexCount, this.gl.UNSIGNED_INT, 0);
  }

  dispose(): void {
    this.vertices.dispose();
    this.indices.dispose();
    this.vao.dispose();
  }
}
