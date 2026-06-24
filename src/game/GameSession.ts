import { GameState } from "../engine/core/GameState.js";

const MIN_RENDER_DISTANCE = 2;
const MAX_RENDER_DISTANCE = 32;

export class GameSession {
  private currentState = GameState.BOOTING;
  private currentRenderDistance: number;

  constructor(renderDistance: number) {
    this.currentRenderDistance = this.clampRenderDistance(renderDistance);
  }

  get state(): GameState {
    return this.currentState;
  }

  get renderDistance(): number {
    return this.currentRenderDistance;
  }

  enterMainMenu(): void {
    this.currentState = GameState.MAIN_MENU;
  }

  startSingleplayer(): void {
    this.currentState = GameState.GENERATING_WORLD;
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
