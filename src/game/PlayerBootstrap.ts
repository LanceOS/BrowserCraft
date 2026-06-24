import { GameState } from "../engine/core/GameState.js";
import { InputState } from "../engine/core/InputState.js";
import { GameSession } from "./GameSession.js";

export function bootstrapPlayerControls(
  canvas: HTMLCanvasElement,
  input: InputState,
  session: GameSession,
): () => void {
  const onCanvasClick = (): void => {
    if (session.state !== GameState.IN_GAME) return;
    if (!input.pointerLocked) void canvas.requestPointerLock();
  };

  const onPointerLockChange = (): void => {
    input.pointerLocked = document.pointerLockElement === canvas;
    if (!input.pointerLocked) input.clearMovementState();
  };

  const onMouseMove = (event: MouseEvent): void => {
    if (!input.pointerLocked) return;
    input.mouseDelta[0] += event.movementX;
    input.mouseDelta[1] += event.movementY;
  };

  const onKeyDown = (event: KeyboardEvent): void => input.setKey(event.code, true);
  const onKeyUp = (event: KeyboardEvent): void => input.setKey(event.code, false);
  const onMouseDown = (event: MouseEvent): void => input.setMouseButton(event.button, true);
  const onMouseUp = (event: MouseEvent): void => input.setMouseButton(event.button, false);

  canvas.addEventListener("click", onCanvasClick);
  document.addEventListener("pointerlockchange", onPointerLockChange);
  document.addEventListener("mousemove", onMouseMove);
  document.addEventListener("keydown", onKeyDown);
  document.addEventListener("keyup", onKeyUp);
  document.addEventListener("mousedown", onMouseDown);
  document.addEventListener("mouseup", onMouseUp);

  return () => {
    canvas.removeEventListener("click", onCanvasClick);
    document.removeEventListener("pointerlockchange", onPointerLockChange);
    document.removeEventListener("mousemove", onMouseMove);
    document.removeEventListener("keydown", onKeyDown);
    document.removeEventListener("keyup", onKeyUp);
    document.removeEventListener("mousedown", onMouseDown);
    document.removeEventListener("mouseup", onMouseUp);
  };
}
