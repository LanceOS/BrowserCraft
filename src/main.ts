import { DefaultConfig } from "./engine/core/Config.js";
import { Game } from "./game/Game.js";

const root = document.getElementById("app");
if (!root) {
  throw new Error("Missing #app container");
}

const canvas = document.createElement("canvas");
canvas.setAttribute("aria-label", "Voxel sandbox viewport");
root.appendChild(canvas);

const game = new Game(DefaultConfig, canvas);
game.start();

window.addEventListener("beforeunload", () => {
  game.dispose();
});
