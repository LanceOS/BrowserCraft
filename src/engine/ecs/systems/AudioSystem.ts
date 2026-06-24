import type { ComponentStore } from "../ComponentStore.js";
import type { System } from "../SystemManager.js";
import { TransformDesc } from "../components/Transform.js";
import { AudioEmitterDesc } from "../components/AudioEmitter.js";
import { AudioNodePool } from "../../audio/AudioNodePool.js";
import { AudioRegistry, SoundId } from "../../../content/audio/AudioRegistry.js";
import type { AudioListenerView } from "../../render/CameraView.js";

type ModernAudioListener = AudioListener & {
  readonly positionX: AudioParam;
  readonly positionY: AudioParam;
  readonly positionZ: AudioParam;
  readonly forwardX: AudioParam;
  readonly forwardY: AudioParam;
  readonly forwardZ: AudioParam;
  readonly upX: AudioParam;
  readonly upY: AudioParam;
  readonly upZ: AudioParam;
};

type LegacyAudioListener = AudioListener & {
  setPosition(x: number, y: number, z: number): void;
  setOrientation(
    x: number,
    y: number,
    z: number,
    upX: number,
    upY: number,
    upZ: number,
  ): void;
};

export class AudioSystem<TState> implements System<TState> {
  readonly name = "audio";
  readonly stage = "postPhysics" as const;
  private readonly pannerNodes: (PannerNode | null)[] = [];

  constructor(
    private readonly transforms: ComponentStore<typeof TransformDesc>,
    private readonly emitters: ComponentStore<typeof AudioEmitterDesc>,
    private readonly camera: AudioListenerView,
    private readonly ctx: AudioContext,
    private readonly pool: AudioNodePool,
    private readonly registry: AudioRegistry,
  ) {}

  update(_state: TState, dt: number): void {
    this.syncListener();
    const cooldowns = this.emitters.data.cooldown;
    const positions = this.transforms.data.position;

    for (const row of this.emitters.rows()) {
      if (cooldowns[row] > 0) cooldowns[row] = Math.max(0, cooldowns[row] - dt);
      let panner = this.pannerNodes[row];
      if (!panner) {
        panner = this.pool.acquirePanner();
        this.pannerNodes[row] = panner;
      }

      const entityIndex = this.emitters.entityAtRow(row);
      const transformRow = this.transforms.rowFor(entityIndex);
      if (transformRow === -1) continue;
      const base = transformRow * 3;
      panner.positionX.value = positions[base + 0];
      panner.positionY.value = positions[base + 1];
      panner.positionZ.value = positions[base + 2];
    }
  }

  playEntitySound(entityIndex: number, soundId: SoundId): void {
    const row = this.emitters.rowFor(entityIndex);
    if (row === -1 || this.emitters.data.cooldown[row] > 0) return;

    const buffer = this.registry.get(soundId);
    if (!buffer) return;

    let panner = this.pannerNodes[row];
    if (!panner) {
      panner = this.pool.acquirePanner();
      this.pannerNodes[row] = panner;
    }

    void this.pool.resume();
    const source = this.ctx.createBufferSource();
    source.buffer = buffer;
    source.playbackRate.value = this.emitters.data.pitch[row] * (0.92 + Math.random() * 0.16);

    const gain = this.ctx.createGain();
    gain.gain.value = this.emitters.data.volume[row];
    source.connect(gain);
    gain.connect(panner);
    source.onended = () => {
      source.disconnect();
      gain.disconnect();
    };
    source.start();
    this.emitters.data.cooldown[row] = 1;
  }

  dispose(): void {
    for (const panner of this.pannerNodes) {
      if (panner) this.pool.releasePanner(panner);
    }
    this.pannerNodes.length = 0;
  }

  private syncListener(): void {
    const listener = this.ctx.listener;
    const modern = listener as Partial<ModernAudioListener>;
    const legacy = listener as Partial<LegacyAudioListener>;
    const x = this.camera.position[0];
    const y = this.camera.position[1];
    const z = this.camera.position[2];

    if (modern.positionX && modern.positionY && modern.positionZ) {
      modern.positionX.value = x;
      modern.positionY.value = y;
      modern.positionZ.value = z;
    } else if (legacy.setPosition) {
      legacy.setPosition(x, y, z);
    }

    if (modern.forwardX && modern.forwardY && modern.forwardZ && modern.upX && modern.upY && modern.upZ) {
      modern.forwardX.value = this.camera.forward[0];
      modern.forwardY.value = this.camera.forward[1];
      modern.forwardZ.value = this.camera.forward[2];
      modern.upX.value = this.camera.up[0];
      modern.upY.value = this.camera.up[1];
      modern.upZ.value = this.camera.up[2];
    } else if (legacy.setOrientation) {
      legacy.setOrientation(
        this.camera.forward[0],
        this.camera.forward[1],
        this.camera.forward[2],
        this.camera.up[0],
        this.camera.up[1],
        this.camera.up[2],
      );
    }
  }
}
