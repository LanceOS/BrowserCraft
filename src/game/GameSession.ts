import { GameState } from "../engine/core/GameState.js";

export const MIN_RENDER_DISTANCE = 2;
export const MAX_RENDER_DISTANCE = 4;

export type GameMode = "survival" | "creative";

export class GameSession {
  private currentState = GameState.BOOTING;
  private currentRenderDistance: number;
  private currentGameMode: GameMode = "survival";
  private currentStartRequestId = 0;

  constructor(renderDistance: number) {
    this.currentRenderDistance = this.clampRenderDistance(renderDistance);
  }

  get state(): GameState {
    return this.currentState;
  }

  get renderDistance(): number {
    return this.currentRenderDistance;
  }

  get gameMode(): GameMode {
    return this.currentGameMode;
  }

  get startRequestId(): number {
    return this.currentStartRequestId;
  }

  enterMainMenu(): void {
    this.currentState = GameState.MAIN_MENU;
  }

  startSingleplayer(gameMode: GameMode): void {
    this.currentGameMode = gameMode;
    this.currentState = GameState.GENERATING_WORLD;
    this.currentStartRequestId++;
  }

  markWorldReady(): boolean {
    if (this.currentState !== GameState.GENERATING_WORLD) return false;
    this.currentState = GameState.IN_GAME;
    return true;
  }

  pause(): boolean {
    if (this.currentState !== GameState.IN_GAME) return false;
    this.currentState = GameState.PAUSED;
    return true;
  }

  resume(): boolean {
    if (this.currentState !== GameState.PAUSED) return false;
    this.currentState = GameState.IN_GAME;
    return true;
  }

  returnToTitle(): void {
    this.currentState = GameState.MAIN_MENU;
  }

  setRenderDistance(renderDistance: number): void {
    this.currentRenderDistance = this.clampRenderDistance(renderDistance);
  }

  private clampRenderDistance(renderDistance: number): number {
    const normalized = Number.isFinite(renderDistance) ? Math.floor(renderDistance) : MIN_RENDER_DISTANCE;
    return Math.max(MIN_RENDER_DISTANCE, Math.min(MAX_RENDER_DISTANCE, normalized));
  }
}
