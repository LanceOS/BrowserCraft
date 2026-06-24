import { GameContext, GameState } from "./GameState.js";
import { InputState } from "./InputState.js";
import { UIManager } from "../../ui/UIManager.js";

export class GameLoop {
  private readonly fixedStep: number;
  private accumulator = 0;
  private lastTime = 0;
  private running = false;
  private frameHandle = 0;
  private readonly onEscape = (event: KeyboardEvent): void => {
    if (event.code !== "Escape") return;
    this.ui.handleAction("toggle-pause");
  };

  constructor(
    targetFps: number,
    private readonly ui: UIManager,
    private readonly input: InputState,
    private readonly update: (dt: number) => void,
    private readonly render: (alpha: number, timeSeconds: number) => void,
  ) {
    this.fixedStep = 1 / targetFps;
  }

  start(): void {
    if (this.running) return;
    this.running = true;
    this.lastTime = performance.now();
    this.ui.showMainMenu();
    document.addEventListener("keydown", this.onEscape);
    this.frameHandle = requestAnimationFrame(this.tick);
  }

  stop(): void {
    if (!this.running) return;
    this.running = false;
    document.removeEventListener("keydown", this.onEscape);
    cancelAnimationFrame(this.frameHandle);
  }

  private readonly tick = (time: number): void => {
    if (!this.running) return;

    const seconds = time * 0.001;
    const lastSeconds = this.lastTime * 0.001;
    let frameDt = seconds - lastSeconds;
    this.lastTime = time;
    frameDt = Math.min(frameDt, 0.1);
    const updatesEnabled =
      GameContext.state === GameState.IN_GAME ||
      GameContext.state === GameState.GENERATING_WORLD;

    if (updatesEnabled) {
      this.accumulator += frameDt;
      while (this.accumulator >= this.fixedStep) {
        this.update(this.fixedStep);
        this.accumulator -= this.fixedStep;
      }
    } else {
      this.accumulator = 0;
    }

    this.render(this.accumulator / this.fixedStep, seconds);
    this.input.clearFrameState();
    this.frameHandle = requestAnimationFrame(this.tick);
  };
}
