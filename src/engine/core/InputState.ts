const KEY_MAP: Record<string, number> = {
  KeyW: 0,
  KeyA: 1,
  KeyS: 2,
  KeyD: 3,
  Space: 4,
  ShiftLeft: 5,
  ShiftRight: 5,
  ControlLeft: 6,
  Escape: 7,
  KeyE: 8,
  Digit1: 9,
  Digit2: 10,
  Digit3: 11,
  Digit4: 12,
  Digit5: 13,
  Digit6: 14,
  Digit7: 15,
  Digit8: 16,
  Digit9: 17,
};

export class InputState {
  readonly keys = new Uint8Array(32);
  readonly mouseDelta = new Float32Array(2);
  readonly mouseButtons = new Uint8Array(3);
  pointerLocked = false;

  setKey(code: string, isDown: boolean): void {
    const idx = KEY_MAP[code];
    if (idx === undefined) return;
    this.keys[idx] = isDown ? (this.keys[idx] > 0 ? 2 : 1) : 0;
  }

  setMouseButton(button: number, isDown: boolean): void {
    if (button < 0 || button >= this.mouseButtons.length) return;
    this.mouseButtons[button] = isDown ? 1 : 0;
  }

  clearFrameState(): void {
    for (let i = 0; i < this.keys.length; i++) {
      if (this.keys[i] === 1) this.keys[i] = 2;
    }
    this.mouseDelta[0] = 0;
    this.mouseDelta[1] = 0;
  }

  clearMovementState(): void {
    this.keys.fill(0);
    this.mouseDelta[0] = 0;
    this.mouseDelta[1] = 0;
  }

  isPressed(idx: number): boolean {
    return this.keys[idx] === 1;
  }

  isHeld(idx: number): boolean {
    return this.keys[idx] > 0;
  }

  isPressedCode(code: string): boolean {
    const idx = KEY_MAP[code];
    return idx !== undefined ? this.keys[idx] === 1 : false;
  }

  isHeldCode(code: string): boolean {
    const idx = KEY_MAP[code];
    return idx !== undefined ? this.keys[idx] > 0 : false;
  }
}
