import { UniformBuffer } from "../../render/UniformBuffer.js";

export class TimeSystem {
  private elapsedTime = 0;
  private timeOfDay = 0.25;
  private skyDarknessValue = 0;
  private sunAngleValue = -Math.PI * 0.5;
  private lightLevelValue = 4;
  private readonly sunDirection = new Float32Array([0, -1, 0.2]);
  private readonly dayDuration = 1200;
  private timeScale = 1;
  private readonly data = new Float32Array(8);

  constructor(private readonly ubo: UniformBuffer) {
    this.upload();
  }

  update(dt: number): void {
    this.elapsedTime += dt;
    this.timeOfDay = (this.timeOfDay + (dt / this.dayDuration) * this.timeScale) % 1;
    this.sunAngleValue = this.timeOfDay * Math.PI * 2 - Math.PI * 0.5;
    this.sunDirection[0] = Math.cos(this.sunAngleValue);
    this.sunDirection[1] = Math.sin(this.sunAngleValue);
    this.sunDirection[2] = 0.2;
    let darkness = 0;
    if (this.timeOfDay > 0.2 && this.timeOfDay < 0.8) {
      const dayT = (this.timeOfDay - 0.2) / 0.6;
      darkness = Math.sin(dayT * Math.PI);
    }
    this.skyDarknessValue = Math.max(0, Math.min(1, darkness * 1.2));
    this.lightLevelValue = 4 + this.skyDarknessValue * 11;
    this.upload();
  }

  get skyDarkness(): number {
    return this.skyDarknessValue;
  }

  get sunAngle(): number {
    return this.sunAngleValue;
  }

  get currentTimeOfDay(): number {
    return this.timeOfDay;
  }

  get lightLevel(): number {
    return this.lightLevelValue;
  }

  get isNight(): boolean {
    return this.skyDarknessValue < 0.5;
  }

  skipToMorning(): void {
    if (this.timeOfDay > 0 && this.timeOfDay < 0.25) {
      this.timeOfDay = 0.25;
      this.upload();
    }
  }

  private upload(): void {
    this.data[0] = this.elapsedTime;
    this.data[1] = this.sunAngleValue;
    this.data[2] = this.skyDarknessValue;
    this.data[3] = this.lightLevelValue;
    this.data[4] = this.sunDirection[0];
    this.data[5] = this.sunDirection[1];
    this.data[6] = this.sunDirection[2];
    this.data[7] = 0;
    this.ubo.upload(this.data);
  }
}
