export class InputState {
  private readonly pressed = new Set<string>();
  private lookDeltaX = 0;
  private lookDeltaY = 0;
  private pointerLocked = false;

  constructor(private readonly canvas: HTMLCanvasElement) {
    window.addEventListener("keydown", this.onKeyDown);
    window.addEventListener("keyup", this.onKeyUp);
    window.addEventListener("mousemove", this.onMouseMove);
    document.addEventListener("pointerlockchange", this.onPointerLockChange);
    canvas.addEventListener("click", this.onCanvasClick);
  }

  isPressed(code: string): boolean {
    return this.pressed.has(code);
  }

  consumeLook(): [number, number] {
    const dx = this.lookDeltaX;
    const dy = this.lookDeltaY;
    this.lookDeltaX = 0;
    this.lookDeltaY = 0;
    return [dx, dy];
  }

  dispose(): void {
    window.removeEventListener("keydown", this.onKeyDown);
    window.removeEventListener("keyup", this.onKeyUp);
    window.removeEventListener("mousemove", this.onMouseMove);
    document.removeEventListener("pointerlockchange", this.onPointerLockChange);
    this.canvas.removeEventListener("click", this.onCanvasClick);
  }

  private readonly onKeyDown = (event: KeyboardEvent): void => {
    this.pressed.add(event.code);
  };

  private readonly onKeyUp = (event: KeyboardEvent): void => {
    this.pressed.delete(event.code);
  };

  private readonly onMouseMove = (event: MouseEvent): void => {
    if (!this.pointerLocked) return;
    this.lookDeltaX += event.movementX;
    this.lookDeltaY += event.movementY;
  };

  private readonly onPointerLockChange = (): void => {
    this.pointerLocked = document.pointerLockElement === this.canvas;
  };

  private readonly onCanvasClick = (): void => {
    void this.canvas.requestPointerLock();
  };
}
