import type { System } from "../SystemManager.js";
import type { BlockRegistry } from "../../../world/BlockRegistry.js";
import { Renderer } from "../../render/Renderer.js";
import { ShaderProgram } from "../../render/ShaderProgram.js";
import { particleVertexShaderSource, particleFragmentShaderSource } from "../../render/shaders/particle.js";
import { Tex } from "../../../world/blocks/TextureLayers.js";

const MAX_PARTICLES = 16384;
const INSTANCE_STRIDE_BYTES = 24;

export class ParticleSystem<TState> implements System<TState> {
  readonly name = "particles";
  readonly stage = "postPhysics" as const;
  private readonly positions = new Float32Array(MAX_PARTICLES * 3);
  private readonly velocities = new Float32Array(MAX_PARTICLES * 3);
  private readonly colors = new Uint8Array(MAX_PARTICLES * 4);
  private readonly sizes = new Float32Array(MAX_PARTICLES);
  private readonly lives = new Float32Array(MAX_PARTICLES);
  private readonly maxLives = new Float32Array(MAX_PARTICLES);
  private readonly texLayers = new Float32Array(MAX_PARTICLES);
  private particleCount = 0;

  private readonly gl: WebGL2RenderingContext;
  private readonly shader: ShaderProgram;
  private readonly vao: WebGLVertexArrayObject;
  private readonly instanceVbo: WebGLBuffer;
  private readonly interleavedData: Uint8Array;
  private readonly interleavedF32: Float32Array;

  constructor(
    private readonly renderer: Renderer,
    private readonly blocks: BlockRegistry,
  ) {
    this.gl = renderer.glContext;
    this.shader = new ShaderProgram(this.gl, particleVertexShaderSource, particleFragmentShaderSource);
    this.shader.bindUniformBlock("CameraBlock", 0);

    const vao = this.gl.createVertexArray();
    const quadVbo = this.gl.createBuffer();
    const instanceVbo = this.gl.createBuffer();
    if (!vao || !quadVbo || !instanceVbo) throw new Error("Failed to create particle resources");
    this.vao = vao;
    this.instanceVbo = instanceVbo;

    const buffer = new ArrayBuffer(MAX_PARTICLES * INSTANCE_STRIDE_BYTES);
    this.interleavedData = new Uint8Array(buffer);
    this.interleavedF32 = new Float32Array(buffer);

    this.gl.bindVertexArray(this.vao);

    this.gl.bindBuffer(this.gl.ARRAY_BUFFER, quadVbo);
    this.gl.bufferData(this.gl.ARRAY_BUFFER, new Float32Array([0, 0, 1, 0, 0, 1, 1, 1]), this.gl.STATIC_DRAW);
    this.gl.enableVertexAttribArray(0);
    this.gl.vertexAttribPointer(0, 2, this.gl.FLOAT, false, 0, 0);

    this.gl.bindBuffer(this.gl.ARRAY_BUFFER, this.instanceVbo);
    this.gl.bufferData(this.gl.ARRAY_BUFFER, MAX_PARTICLES * INSTANCE_STRIDE_BYTES, this.gl.DYNAMIC_DRAW);
    this.gl.enableVertexAttribArray(1);
    this.gl.vertexAttribPointer(1, 3, this.gl.FLOAT, false, INSTANCE_STRIDE_BYTES, 0);
    this.gl.vertexAttribDivisor(1, 1);
    this.gl.enableVertexAttribArray(2);
    this.gl.vertexAttribPointer(2, 4, this.gl.UNSIGNED_BYTE, true, INSTANCE_STRIDE_BYTES, 12);
    this.gl.vertexAttribDivisor(2, 1);
    this.gl.enableVertexAttribArray(3);
    this.gl.vertexAttribPointer(3, 2, this.gl.FLOAT, false, INSTANCE_STRIDE_BYTES, 16);
    this.gl.vertexAttribDivisor(3, 1);

    this.gl.bindVertexArray(null);
    this.gl.bindBuffer(this.gl.ARRAY_BUFFER, null);
  }

  update(_state: TState, dt: number): void {
    const gravity = -20;
    const drag = 0.98;

    for (let i = this.particleCount - 1; i >= 0; i--) {
      this.lives[i] -= dt;
      if (this.lives[i] <= 0) {
        this.swapPop(i);
        continue;
      }

      const base = i * 3;
      this.velocities[base + 1] += gravity * dt;
      this.velocities[base + 0] *= drag;
      this.velocities[base + 1] *= drag;
      this.velocities[base + 2] *= drag;

      this.positions[base + 0] += this.velocities[base + 0] * dt;
      this.positions[base + 1] += this.velocities[base + 1] * dt;
      this.positions[base + 2] += this.velocities[base + 2] * dt;

      const lifeRatio = this.lives[i] / this.maxLives[i];
      this.sizes[i] *= 0.9 + 0.1 * lifeRatio;
      this.colors[i * 4 + 3] = Math.max(16, Math.floor(255 * lifeRatio));
    }

    for (let i = 0; i < this.particleCount; i++) {
      const offset = i * INSTANCE_STRIDE_BYTES;
      const base = i * 3;
      this.interleavedF32[offset / 4 + 0] = this.positions[base + 0];
      this.interleavedF32[offset / 4 + 1] = this.positions[base + 1];
      this.interleavedF32[offset / 4 + 2] = this.positions[base + 2];
      this.interleavedData[offset + 12] = this.colors[i * 4 + 0];
      this.interleavedData[offset + 13] = this.colors[i * 4 + 1];
      this.interleavedData[offset + 14] = this.colors[i * 4 + 2];
      this.interleavedData[offset + 15] = this.colors[i * 4 + 3];
      this.interleavedF32[offset / 4 + 4] = this.sizes[i];
      this.interleavedF32[offset / 4 + 5] = this.texLayers[i];
    }
  }

  spawnBlockBreak(x: number, y: number, z: number, blockId: number): void {
    const def = this.blocks.tryGet(blockId);
    if (!def) return;
    const tint = this.getParticleTint(blockId, def.textures.side);
    const count = 4 + Math.floor(Math.random() * 4);
    for (let i = 0; i < count; i++) {
      if (this.particleCount >= MAX_PARTICLES) break;
      const idx = this.particleCount++;
      const base = idx * 3;
      this.positions[base + 0] = x + 0.2 + Math.random() * 0.6;
      this.positions[base + 1] = y + 0.2 + Math.random() * 0.6;
      this.positions[base + 2] = z + 0.2 + Math.random() * 0.6;
      this.velocities[base + 0] = (Math.random() - 0.5) * 0.45;
      this.velocities[base + 1] = Math.random() * 0.45 + 0.12;
      this.velocities[base + 2] = (Math.random() - 0.5) * 0.45;
      this.colors[idx * 4 + 0] = tint[0];
      this.colors[idx * 4 + 1] = tint[1];
      this.colors[idx * 4 + 2] = tint[2];
      this.colors[idx * 4 + 3] = 255;
      this.sizes[idx] = 0.08 + Math.random() * 0.05;
      this.lives[idx] = 0.45 + Math.random() * 0.45;
      this.maxLives[idx] = this.lives[idx];
      this.texLayers[idx] = def.textures.side;
    }
  }

  render(): void {
    if (this.particleCount === 0) return;
    this.gl.bindBuffer(this.gl.ARRAY_BUFFER, this.instanceVbo);
    this.gl.bufferSubData(this.gl.ARRAY_BUFFER, 0, this.interleavedData, 0, this.particleCount * INSTANCE_STRIDE_BYTES);
    this.shader.use();
    this.renderer.bindBlockTextures(0);
    this.gl.uniform1i(this.shader.uniform("u_blockTextures"), 0);
    this.gl.bindVertexArray(this.vao);
    this.gl.enable(this.gl.BLEND);
    this.gl.blendFunc(this.gl.SRC_ALPHA, this.gl.ONE_MINUS_SRC_ALPHA);
    this.gl.disable(this.gl.CULL_FACE);
    this.gl.drawArraysInstanced(this.gl.TRIANGLE_STRIP, 0, 4, this.particleCount);
    this.gl.enable(this.gl.CULL_FACE);
    this.gl.bindVertexArray(null);
  }

  dispose(): void {
    this.gl.deleteBuffer(this.instanceVbo);
    this.gl.deleteVertexArray(this.vao);
    this.shader.dispose();
  }

  private swapPop(index: number): void {
    const last = this.particleCount - 1;
    if (index !== last) {
      const dst3 = index * 3;
      const src3 = last * 3;
      this.positions[dst3 + 0] = this.positions[src3 + 0];
      this.positions[dst3 + 1] = this.positions[src3 + 1];
      this.positions[dst3 + 2] = this.positions[src3 + 2];
      this.velocities[dst3 + 0] = this.velocities[src3 + 0];
      this.velocities[dst3 + 1] = this.velocities[src3 + 1];
      this.velocities[dst3 + 2] = this.velocities[src3 + 2];
      this.colors[index * 4 + 0] = this.colors[last * 4 + 0];
      this.colors[index * 4 + 1] = this.colors[last * 4 + 1];
      this.colors[index * 4 + 2] = this.colors[last * 4 + 2];
      this.colors[index * 4 + 3] = this.colors[last * 4 + 3];
      this.sizes[index] = this.sizes[last];
      this.lives[index] = this.lives[last];
      this.maxLives[index] = this.maxLives[last];
      this.texLayers[index] = this.texLayers[last];
    }
    this.particleCount--;
  }

  private getParticleTint(blockId: number, textureLayer: number): [number, number, number] {
    if (blockId === 2 || blockId === 18 || textureLayer === Tex.GRASS_SIDE || textureLayer === Tex.LEAVES_OAK) {
      return [102, 154, 74];
    }
    if (blockId === 3 || textureLayer === Tex.DIRT) {
      return [126, 84, 58];
    }
    if (textureLayer === Tex.SAND || textureLayer === Tex.SANDSTONE) {
      return [214, 198, 132];
    }
    if (textureLayer === Tex.WATER) {
      return [72, 134, 210];
    }
    if (textureLayer === Tex.LAVA || textureLayer === Tex.REDSTONE_LAMP_ON) {
      return [236, 156, 74];
    }
    if (textureLayer === Tex.PLANKS_OAK || textureLayer === Tex.LOG_OAK_SIDE) {
      return [160, 124, 68];
    }
    if (textureLayer >= Tex.REDSTONE_WIRE_0 && textureLayer <= Tex.REDSTONE_WIRE_15) {
      return [188, 54, 36];
    }
    return [124, 124, 124];
  }
}
