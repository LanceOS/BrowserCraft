import type { GameConfig } from "../core/Config.js";
import { UniformBuffer } from "./UniformBuffer.js";
import { ShaderProgram } from "./ShaderProgram.js";
import { Texture2DArray } from "./Texture2DArray.js";
import { FrustumCuller } from "./FrustumCuller.js";
import { ChunkMesh } from "./ChunkMesh.js";
import { chunkVertexShaderSource, chunkFragmentShaderSource } from "./shaders/chunk.js";
import { skyVertexShaderSource, skyFragmentShaderSource } from "./shaders/sky.js";
import type { CameraView } from "./CameraView.js";
import { TexturePipeline } from "../assets/TexturePipeline.js";
import type { BlockRegistry } from "../../world/BlockRegistry.js";
import type { World } from "../../world/World.js";

const CAMERA_BLOCK_FLOATS = 80;
const TIME_BLOCK_FLOATS = 8;

export class Renderer {
  private readonly vertexStrideFloats = 10;
  private readonly chunkShader: ShaderProgram;
  private readonly skyShader: ShaderProgram;
  private readonly cameraUbo: UniformBuffer;
  private readonly timeUbo: UniformBuffer;
  private readonly textures: Texture2DArray;
  private readonly texturePipeline = new TexturePipeline();
  private readonly frustum = new FrustumCuller();
  private readonly meshes = new Map<string, ChunkMesh>();
  private readonly cameraBlock = new Float32Array(CAMERA_BLOCK_FLOATS);
  private readonly frustumMin = new Float32Array(3);
  private readonly frustumMax = new Float32Array(3);
  private readonly skyVao: WebGLVertexArrayObject;
  private readonly skyVbo: WebGLBuffer;

  constructor(
    private readonly gl: WebGL2RenderingContext,
    blocks: BlockRegistry,
    private readonly config: GameConfig,
  ) {
    this.chunkShader = new ShaderProgram(gl, chunkVertexShaderSource, chunkFragmentShaderSource);
    this.skyShader = new ShaderProgram(gl, skyVertexShaderSource, skyFragmentShaderSource);
    this.chunkShader.bindUniformBlock("CameraBlock", 0);
    this.chunkShader.bindUniformBlock("TimeBlock", 2);
    this.skyShader.bindUniformBlock("CameraBlock", 0);
    this.skyShader.bindUniformBlock("TimeBlock", 2);
    this.cameraUbo = new UniformBuffer(gl, 0, CAMERA_BLOCK_FLOATS * 4);
    this.timeUbo = new UniformBuffer(gl, 2, TIME_BLOCK_FLOATS * 4);
    this.textures = new Texture2DArray(gl, 16, 16, config.textureArrayLayers);
    this.seedTextureArray(blocks);
    const skyVao = gl.createVertexArray();
    const skyVbo = gl.createBuffer();
    if (!skyVao || !skyVbo) throw new Error("Failed to create sky resources");
    this.skyVao = skyVao;
    this.skyVbo = skyVbo;
    gl.bindVertexArray(this.skyVao);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.skyVbo);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);
    gl.bindBuffer(gl.ARRAY_BUFFER, null);

    gl.enable(gl.DEPTH_TEST);
    gl.disable(gl.CULL_FACE);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
  }

  get glContext(): WebGL2RenderingContext {
    return this.gl;
  }

  getTimeUbo(): UniformBuffer {
    return this.timeUbo;
  }

  bindBlockTextures(unit = 0): void {
    this.textures.bind(unit);
  }

  resizeCanvasToDisplaySize(): number {
    this.resizeCanvas();
    return this.gl.canvas.width / this.gl.canvas.height;
  }

  render(world: World, camera: CameraView, timeSeconds: number, daylightFactor: number): void {

    this.syncChunks(world);

    this.gl.viewport(0, 0, this.gl.canvas.width, this.gl.canvas.height);
    const skyR = 0.08 + 0.5 * daylightFactor;
    const skyG = 0.1 + 0.64 * daylightFactor;
    const skyB = 0.16 + 0.74 * daylightFactor;
    this.gl.clearColor(0, 0, 0, 1);
    this.gl.clear(this.gl.COLOR_BUFFER_BIT | this.gl.DEPTH_BUFFER_BIT);

    this.uploadCameraBlock(camera, timeSeconds, daylightFactor, skyR, skyG, skyB);
    this.renderSky();

    this.chunkShader.use();
    this.bindBlockTextures(0);
    this.gl.uniform1i(this.chunkShader.uniform("u_blockTextures"), 0);

    this.frustum.extractFrom(camera.viewProjectionMatrix);

    for (const [key, chunk] of world.entries()) {
      const mesh = this.meshes.get(key);
      if (!mesh || mesh.indexCount === 0) continue;

      this.frustumMin[0] = chunk.chunkX * this.config.chunkSize;
      this.frustumMin[1] = 0;
      this.frustumMin[2] = chunk.chunkZ * this.config.chunkSize;
      this.frustumMax[0] = this.frustumMin[0] + this.config.chunkSize;
      this.frustumMax[1] = this.config.worldHeight;
      this.frustumMax[2] = this.frustumMin[2] + this.config.chunkSize;

      if (!this.frustum.testAABB(this.frustumMin, this.frustumMax)) continue;

      this.gl.uniform3f(
        this.chunkShader.uniform("u_chunkTranslation"),
        chunk.chunkX * this.config.chunkSize,
        0,
        chunk.chunkZ * this.config.chunkSize,
      );
      mesh.draw();
    }
  }

  dispose(): void {
    for (const mesh of this.meshes.values()) mesh.dispose();
    this.meshes.clear();
    this.textures.dispose();
    this.cameraUbo.dispose();
    this.timeUbo.dispose();
    this.gl.deleteBuffer(this.skyVbo);
    this.gl.deleteVertexArray(this.skyVao);
    this.chunkShader.dispose();
    this.skyShader.dispose();
  }

  private syncChunks(world: World): void {
    for (const [key, mesh] of this.meshes) {
      if (!world.hasChunkKey(key)) {
        mesh.dispose();
        this.meshes.delete(key);
      }
    }

    for (const [key, chunk] of world.entries()) {
      if (chunk.state !== "meshReady") continue;
      if (chunk.indexCount === 0 || chunk.vertexCount === 0) {
        world.markUploaded(chunk);
        continue;
      }

      let mesh = this.meshes.get(key);
      if (!mesh) {
        mesh = new ChunkMesh(this.gl);
        this.meshes.set(key, mesh);
      }

      const slot = world.getChunkSlot(chunk);
      const vertexView = slot.vertices.subarray(0, chunk.vertexCount * this.vertexStrideFloats);
      const indexView = slot.indices.subarray(0, chunk.indexCount);
      mesh.upload(chunk, vertexView, indexView, this.vertexStrideFloats);
      world.markUploaded(chunk);
    }
  }

  private uploadCameraBlock(
    camera: CameraView,
    timeSeconds: number,
    daylightFactor: number,
    skyR: number,
    skyG: number,
    skyB: number,
  ): void {
    this.cameraBlock.set(camera.projectionMatrix, 0);
    this.cameraBlock.set(camera.viewMatrix, 16);
    this.cameraBlock.set(camera.viewProjectionMatrix, 32);
    this.cameraBlock.set(camera.inverseViewProjectionMatrix, 48);
    this.cameraBlock[64] = camera.position[0];
    this.cameraBlock[65] = camera.position[1];
    this.cameraBlock[66] = camera.position[2];
    this.cameraBlock[67] = timeSeconds;
    this.cameraBlock[68] = skyR * (0.55 + 0.45 * daylightFactor);
    this.cameraBlock[69] = skyG * (0.55 + 0.45 * daylightFactor);
    this.cameraBlock[70] = skyB * (0.6 + 0.4 * daylightFactor);
    this.cameraBlock[71] = this.config.renderDistance * this.config.chunkSize * 1.8;
    this.cameraBlock[72] = camera.right[0];
    this.cameraBlock[73] = camera.right[1];
    this.cameraBlock[74] = camera.right[2];
    this.cameraBlock[75] = 0;
    this.cameraBlock[76] = camera.up[0];
    this.cameraBlock[77] = camera.up[1];
    this.cameraBlock[78] = camera.up[2];
    this.cameraBlock[79] = 0;
    this.cameraUbo.upload(this.cameraBlock);
  }

  private renderSky(): void {
    this.gl.depthMask(false);
    this.gl.disable(this.gl.CULL_FACE);
    this.skyShader.use();
    this.gl.bindVertexArray(this.skyVao);
    this.gl.drawArrays(this.gl.TRIANGLES, 0, 3);
    this.gl.bindVertexArray(null);
    this.gl.depthMask(true);
  }

  private seedTextureArray(blocks: BlockRegistry): void {
    for (const layer of this.texturePipeline.createLayerArray(this.config.textureArrayLayers)) {
      this.textures.uploadLayer(layer.layer, layer.pixels, layer.width, layer.height);
    }
    this.textures.generateMipmaps();
    void blocks;
  }

  private resizeCanvas(): void {
    const canvas = this.gl.canvas as HTMLCanvasElement;
    const width = Math.max(1, Math.floor(canvas.clientWidth * window.devicePixelRatio));
    const height = Math.max(1, Math.floor(canvas.clientHeight * window.devicePixelRatio));
    if (canvas.width === width && canvas.height === height) return;
    canvas.width = width;
    canvas.height = height;
  }
}
