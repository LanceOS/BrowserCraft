import type { GameConfig } from "../core/Config.js";
import { UniformBuffer } from "./UniformBuffer.js";
import { ShaderProgram } from "./ShaderProgram.js";
import { Texture2DArray } from "./Texture2DArray.js";
import { FrustumCuller } from "./FrustumCuller.js";
import { ChunkMesh } from "./ChunkMesh.js";
import { chunkVertexShaderSource, chunkFragmentShaderSource } from "./shaders/chunk.js";
import { skyVertexShaderSource, skyFragmentShaderSource } from "./shaders/sky.js";
import type { BlockRegistry } from "../../world/BlockRegistry.js";
import type { World } from "../../world/World.js";
import type { Camera } from "./Camera.js";
import { Tex } from "../../world/blocks/TextureLayers.js";

const CAMERA_BLOCK_FLOATS = 80;
const TIME_BLOCK_FLOATS = 8;

const makeLayer = (r: number, g: number, b: number, a = 255, accent = 0): Uint8Array => {
  const data = new Uint8Array(16 * 16 * 4);
  for (let y = 0; y < 16; y++) {
    for (let x = 0; x < 16; x++) {
      const index = (y * 16 + x) * 4;
      const tint = ((x + y + accent) & 1) === 0 ? 10 : -10;
      data[index + 0] = Math.max(0, Math.min(255, r + tint));
      data[index + 1] = Math.max(0, Math.min(255, g + tint));
      data[index + 2] = Math.max(0, Math.min(255, b + tint));
      data[index + 3] = a;
    }
  }
  return data;
};

const LAYER_COLORS: Record<number, Uint8Array> = {
  [Tex.AIR]: makeLayer(120, 186, 96, 0),
  [Tex.STONE]: makeLayer(126, 132, 138),
  [Tex.GRASS_TOP]: makeLayer(98, 158, 78),
  [Tex.GRASS_SIDE]: makeLayer(92, 120, 58),
  [Tex.DIRT]: makeLayer(126, 84, 58),
  [Tex.COBBLESTONE]: makeLayer(107, 110, 114),
  [Tex.PLANKS_OAK]: makeLayer(182, 151, 82),
  [Tex.LOG_OAK_TOP]: makeLayer(149, 119, 71),
  [Tex.LOG_OAK_SIDE]: makeLayer(108, 82, 51),
  [Tex.LEAVES_OAK]: makeLayer(90, 152, 76, 180, 3),
  [Tex.GLASS]: makeLayer(204, 232, 255, 118, 2),
  [Tex.SAND]: makeLayer(210, 194, 126),
  [Tex.GRAVEL]: makeLayer(136, 131, 126),
  [Tex.COAL_ORE]: makeLayer(112, 112, 116),
  [Tex.IRON_ORE]: makeLayer(166, 136, 108),
  [Tex.GOLD_ORE]: makeLayer(186, 154, 74),
  [Tex.DIAMOND_ORE]: makeLayer(92, 182, 184),
  [Tex.REDSTONE_ORE]: makeLayer(150, 56, 50),
  [Tex.LAPIS_ORE]: makeLayer(72, 96, 176),
  [Tex.BEDROCK]: makeLayer(84, 84, 84),
  [Tex.WATER]: makeLayer(72, 134, 210, 168, 1),
  [Tex.LAVA]: makeLayer(224, 92, 32, 210, 1),
  [Tex.BRICK]: makeLayer(156, 78, 66),
  [Tex.MOSSY_COBBLESTONE]: makeLayer(95, 114, 82),
  [Tex.OBSIDIAN]: makeLayer(44, 32, 62),
  [Tex.CACTUS_TOP]: makeLayer(72, 128, 54),
  [Tex.CACTUS_SIDE]: makeLayer(64, 142, 56),
  [Tex.CACTUS_BOTTOM]: makeLayer(96, 82, 54),
  [Tex.GLOWSTONE]: makeLayer(226, 190, 110),
  [Tex.GOLD_BLOCK]: makeLayer(238, 214, 80),
  [Tex.IRON_BLOCK]: makeLayer(212, 214, 216),
  [Tex.DIAMOND_BLOCK]: makeLayer(98, 224, 214),
  [Tex.CRAFTING_TABLE_TOP]: makeLayer(160, 132, 74),
  [Tex.CRAFTING_TABLE_SIDE]: makeLayer(122, 84, 50),
  [Tex.FURNACE_SIDE]: makeLayer(122, 122, 122),
  [Tex.FURNACE_TOP]: makeLayer(96, 96, 96),
  [Tex.SANDSTONE]: makeLayer(218, 198, 136),
};

for (let power = 0; power < 16; power++) {
  const brightness = 72 + power * 10;
  LAYER_COLORS[Tex.REDSTONE_WIRE_0 + power] = makeLayer(brightness, 18 + power * 4, 18 + power * 2, 220, power);
}
LAYER_COLORS[Tex.REDSTONE_LAMP_OFF] = makeLayer(86, 62, 32);
LAYER_COLORS[Tex.REDSTONE_LAMP_ON] = makeLayer(242, 200, 108);

export class Renderer {
  private readonly vertexStrideFloats = 10;
  private readonly chunkShader: ShaderProgram;
  private readonly skyShader: ShaderProgram;
  private readonly cameraUbo: UniformBuffer;
  private readonly timeUbo: UniformBuffer;
  private readonly textures: Texture2DArray;
  private readonly frustum = new FrustumCuller();
  private readonly meshes = new Map<string, ChunkMesh>();
  private readonly cameraBlock = new Float32Array(CAMERA_BLOCK_FLOATS);
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

  render(world: World, camera: Camera, timeSeconds: number, skyDarkness: number): void {
    this.resizeCanvas();
    camera.resize(this.gl.canvas.width / this.gl.canvas.height, this.config.fovDegrees);
    camera.updateMatrices();

    this.syncChunks(world);

    this.gl.viewport(0, 0, this.gl.canvas.width, this.gl.canvas.height);
    const skyR = 0.08 + 0.5 * skyDarkness;
    const skyG = 0.1 + 0.64 * skyDarkness;
    const skyB = 0.16 + 0.74 * skyDarkness;
    this.gl.clearColor(0, 0, 0, 1);
    this.gl.clear(this.gl.COLOR_BUFFER_BIT | this.gl.DEPTH_BUFFER_BIT);

    this.uploadCameraBlock(camera, timeSeconds, skyDarkness, skyR, skyG, skyB);
    this.renderSky();

    this.chunkShader.use();
    this.bindBlockTextures(0);
    this.gl.uniform1i(this.chunkShader.uniform("u_blockTextures"), 0);

    this.frustum.extractFrom(camera.viewProjectionMatrix);

    const min = new Float32Array(3);
    const max = new Float32Array(3);
    for (const [key, chunk] of world.entries()) {
      const mesh = this.meshes.get(key);
      if (!mesh || mesh.indexCount === 0) continue;

      min[0] = chunk.chunkX * this.config.chunkSize;
      min[1] = 0;
      min[2] = chunk.chunkZ * this.config.chunkSize;
      max[0] = min[0] + this.config.chunkSize;
      max[1] = this.config.worldHeight;
      max[2] = min[2] + this.config.chunkSize;

      if (!this.frustum.testAABB(min, max)) continue;

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
    camera: Camera,
    timeSeconds: number,
    skyDarkness: number,
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
    this.cameraBlock[68] = skyR * (0.55 + 0.45 * skyDarkness);
    this.cameraBlock[69] = skyG * (0.55 + 0.45 * skyDarkness);
    this.cameraBlock[70] = skyB * (0.6 + 0.4 * skyDarkness);
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
    for (let layer = 0; layer < this.config.textureArrayLayers; layer++) {
      this.textures.uploadLayer(layer, LAYER_COLORS[layer] ?? makeLayer(255, 0, 255), 16, 16);
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
