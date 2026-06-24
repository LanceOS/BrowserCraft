export const enum SoundId {
  STONE_BREAK,
  STONE_STEP,
  GRASS_BREAK,
  GRASS_STEP,
  WOOD_BREAK,
  ZOMBIE_GROAN,
  ZOMBIE_HURT,
  SKELETON_HURT,
  CLICK,
  AMBIENT_CAVE,
  MUSIC_CALM1,
}

const clampSample = (value: number): number => Math.max(-1, Math.min(1, value));

const createNoiseBuffer = (
  ctx: AudioContext,
  duration: number,
  decayPower: number,
  toneMix: number,
): AudioBuffer => {
  const frameCount = Math.max(1, Math.floor(ctx.sampleRate * duration));
  const buffer = ctx.createBuffer(1, frameCount, ctx.sampleRate);
  const channel = buffer.getChannelData(0);
  for (let i = 0; i < frameCount; i++) {
    const t = i / frameCount;
    const envelope = Math.pow(1 - t, decayPower);
    const noise = (Math.random() * 2 - 1) * envelope;
    const tone = Math.sin(t * Math.PI * (8 + toneMix * 12)) * envelope * toneMix;
    channel[i] = clampSample(noise * (1 - toneMix) + tone * 0.35);
  }
  return buffer;
};

const createToneBuffer = (
  ctx: AudioContext,
  duration: number,
  baseFreq: number,
  wobble: number,
): AudioBuffer => {
  const frameCount = Math.max(1, Math.floor(ctx.sampleRate * duration));
  const buffer = ctx.createBuffer(1, frameCount, ctx.sampleRate);
  const channel = buffer.getChannelData(0);
  for (let i = 0; i < frameCount; i++) {
    const t = i / ctx.sampleRate;
    const life = i / frameCount;
    const envelope = Math.pow(1 - life, 1.8);
    const freq = baseFreq + Math.sin(t * wobble) * (baseFreq * 0.08);
    const fundamental = Math.sin(t * Math.PI * 2 * freq);
    const overtone = Math.sin(t * Math.PI * 2 * freq * 1.5) * 0.35;
    channel[i] = clampSample((fundamental + overtone) * envelope * 0.45);
  }
  return buffer;
};

export class AudioRegistry {
  private readonly buffers = new Map<SoundId, AudioBuffer>();

  async load(ctx: AudioContext, id: SoundId, data: ArrayBuffer): Promise<void> {
    const audioBuffer = await ctx.decodeAudioData(data);
    this.buffers.set(id, audioBuffer);
  }

  register(id: SoundId, buffer: AudioBuffer): void {
    this.buffers.set(id, buffer);
  }

  get(id: SoundId): AudioBuffer | undefined {
    return this.buffers.get(id);
  }

  seedBuiltinSounds(ctx: AudioContext): void {
    this.register(SoundId.STONE_BREAK, createNoiseBuffer(ctx, 0.18, 2.6, 0.15));
    this.register(SoundId.STONE_STEP, createNoiseBuffer(ctx, 0.07, 3.4, 0.05));
    this.register(SoundId.GRASS_BREAK, createNoiseBuffer(ctx, 0.16, 2.2, 0.32));
    this.register(SoundId.GRASS_STEP, createNoiseBuffer(ctx, 0.06, 2.8, 0.18));
    this.register(SoundId.WOOD_BREAK, createNoiseBuffer(ctx, 0.14, 2.4, 0.48));
    this.register(SoundId.ZOMBIE_GROAN, createToneBuffer(ctx, 0.65, 112, 4));
    this.register(SoundId.ZOMBIE_HURT, createToneBuffer(ctx, 0.24, 176, 8));
    this.register(SoundId.SKELETON_HURT, createToneBuffer(ctx, 0.18, 260, 9));
    this.register(SoundId.CLICK, createToneBuffer(ctx, 0.05, 960, 0));
    this.register(SoundId.AMBIENT_CAVE, createToneBuffer(ctx, 1.2, 84, 1.5));
    this.register(SoundId.MUSIC_CALM1, createToneBuffer(ctx, 2.4, 220, 0.8));
  }
}
