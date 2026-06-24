import { DefaultConfig } from "./engine/core/Config.js";
import { GameState } from "./engine/core/GameState.js";
import { Game } from "./game/Game.js";
import type { GameMode } from "./game/GameSession.js";

const root = document.getElementById("app");
if (!root) {
  throw new Error("Missing #app container");
}

const canvas = document.createElement("canvas");
canvas.setAttribute("aria-label", "Voxel sandbox viewport");
root.appendChild(canvas);

const createWorldSeed = (): number => {
  if (typeof crypto !== "undefined" && typeof crypto.getRandomValues === "function") {
    const values = new Uint32Array(1);
    crypto.getRandomValues(values);
    return values[0];
  }

  return Date.now() >>> 0;
};

const createSaveSlotId = (): string => {
  if (typeof crypto !== "undefined" && typeof crypto.randomUUID === "function") {
    return `world-${crypto.randomUUID()}`;
  }

  return `world-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 10)}`;
};

const createConfig = (worldSeed: number, renderDistance: number) => ({
  ...DefaultConfig,
  renderDistance,
  worldSeed,
});

let game: Game | null = null;

const startSession = (initialState: GameState, worldSeed: number, initialGameMode: GameMode = "survival"): void => {
  const renderDistance = game?.getRenderDistance() ?? DefaultConfig.renderDistance;
  game?.dispose();
  game = new Game(createConfig(worldSeed, renderDistance), canvas, {
    initialState,
    initialGameMode,
    onStartSingleplayer: (gameMode) => startSession(GameState.GENERATING_WORLD, createWorldSeed(), gameMode),
    saveSlotId: createSaveSlotId(),
  });
  game.start();
};

startSession(GameState.MAIN_MENU, DefaultConfig.worldSeed);

window.addEventListener("beforeunload", () => {
  game?.dispose();
});
