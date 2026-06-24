import { UniformBuffer } from "../../render/UniformBuffer.js";

const MORNING_TIME_OF_DAY = 0.25;
const NIGHT_DAYLIGHT_THRESHOLD = 0.3;

const wrapTimeOfDay = (timeOfDay: number): number => {
  const normalized = timeOfDay % 1;
  return normalized < 0 ? normalized + 1 : normalized;
};

export class TimeSystem {
  private elapsedTime = 0;
  private timeOfDay = MORNING_TIME_OF_DAY;
  private daylightFactorValue = 0;
  private sunAngleValue = -Math.PI * 0.5;
  private lightLevelValue = 4;
  private readonly sunDirection = new Float32Array([0, -1, 0.2]);
  private readonly dayDuration = 1200;
  private timeScale = 1;
  private readonly data = new Float32Array(8);

  constructor(private readonly ubo: UniformBuffer) {
    this.syncTimeOfDay(MORNING_TIME_OF_DAY);
  }

  update(dt: number): void {
    this.elapsedTime += dt;
    this.syncTimeOfDay(this.timeOfDay + (dt / this.dayDuration) * this.timeScale);
  }

  get daylightFactor(): number {
    return this.daylightFactorValue;
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
    return this.daylightFactorValue < NIGHT_DAYLIGHT_THRESHOLD;
  }

  skipToMorning(): void {
    if (!this.isNight) return;
    this.syncTimeOfDay(MORNING_TIME_OF_DAY);
  }

  private syncTimeOfDay(timeOfDay: number): void {
    this.timeOfDay = wrapTimeOfDay(timeOfDay);
    this.sunAngleValue = this.timeOfDay * Math.PI * 2 - Math.PI * 0.5;
    this.sunDirection[0] = Math.cos(this.sunAngleValue);
    this.sunDirection[1] = Math.sin(this.sunAngleValue);
    this.sunDirection[2] = 0.2;
    let daylightFactor = 0;
    if (this.timeOfDay > 0.2 && this.timeOfDay < 0.8) {
      const dayT = (this.timeOfDay - 0.2) / 0.6;
      daylightFactor = Math.sin(dayT * Math.PI);
    }
    this.daylightFactorValue = Math.max(0, Math.min(1, daylightFactor * 1.2));
    this.lightLevelValue = 4 + this.daylightFactorValue * 11;
    this.upload();
  }

  private upload(): void {
    this.data[0] = this.elapsedTime;
    this.data[1] = this.sunAngleValue;
    this.data[2] = this.daylightFactorValue;
    this.data[3] = this.lightLevelValue;
    this.data[4] = this.sunDirection[0];
    this.data[5] = this.sunDirection[1];
    this.data[6] = this.sunDirection[2];
    this.data[7] = 0;
    this.ubo.upload(this.data);
  }
}
