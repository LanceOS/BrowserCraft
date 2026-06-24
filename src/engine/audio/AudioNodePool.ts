export class AudioNodePool {
  private readonly masterGain: GainNode;
  private readonly pannerPool: PannerNode[] = [];
  private pannerPoolIndex = 0;

  constructor(private readonly ctx: AudioContext) {
    this.masterGain = ctx.createGain();
    this.masterGain.gain.value = 0.85;
    this.masterGain.connect(ctx.destination);

    const maxPanners = 256;
    for (let i = 0; i < maxPanners; i++) {
      const panner = ctx.createPanner();
      panner.panningModel = "HRTF";
      panner.distanceModel = "exponential";
      panner.refDistance = 1;
      panner.maxDistance = 64;
      panner.rolloffFactor = 1.5;
      panner.connect(this.masterGain);
      this.pannerPool.push(panner);
    }
  }

  acquirePanner(): PannerNode {
    if (this.pannerPoolIndex >= this.pannerPool.length) {
      const panner = this.ctx.createPanner();
      panner.panningModel = "HRTF";
      panner.distanceModel = "exponential";
      panner.refDistance = 1;
      panner.maxDistance = 64;
      panner.rolloffFactor = 1.5;
      panner.connect(this.masterGain);
      return panner;
    }
    return this.pannerPool[this.pannerPoolIndex++];
  }

  releasePanner(panner: PannerNode): void {
    panner.positionX.value = 0;
    panner.positionY.value = 0;
    panner.positionZ.value = 0;
    if (this.pannerPoolIndex > 0) {
      this.pannerPool[--this.pannerPoolIndex] = panner;
    }
  }

  async resume(): Promise<void> {
    if (this.ctx.state === "running") return;
    try {
      await this.ctx.resume();
    } catch {
      // Browser autoplay policies may still block until a user gesture.
    }
  }

  playOneShot(
    buffer: AudioBuffer,
    x: number,
    y: number,
    z: number,
    volume: number,
    pitch: number,
  ): void {
    void this.resume();
    const source = this.ctx.createBufferSource();
    source.buffer = buffer;
    source.playbackRate.value = pitch;

    const gain = this.ctx.createGain();
    gain.gain.value = volume;

    const panner = this.ctx.createPanner();
    panner.panningModel = "HRTF";
    panner.distanceModel = "linear";
    panner.refDistance = 1;
    panner.maxDistance = 32;
    panner.rolloffFactor = 1.2;
    panner.positionX.value = x;
    panner.positionY.value = y;
    panner.positionZ.value = z;

    source.connect(gain);
    gain.connect(panner);
    panner.connect(this.masterGain);
    source.onended = () => {
      source.disconnect();
      gain.disconnect();
      panner.disconnect();
    };
    source.start();
  }

  playUI(buffer: AudioBuffer, volume: number): void {
    void this.resume();
    const source = this.ctx.createBufferSource();
    source.buffer = buffer;
    const gain = this.ctx.createGain();
    gain.gain.value = volume;
    source.connect(gain);
    gain.connect(this.masterGain);
    source.onended = () => {
      source.disconnect();
      gain.disconnect();
    };
    source.start();
  }

  dispose(): void {
    for (const panner of this.pannerPool) {
      panner.disconnect();
    }
    this.masterGain.disconnect();
  }
}
