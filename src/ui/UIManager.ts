import { GameContext, GameState } from "../engine/core/GameState.js";

export class UIManager {
  private readonly root: HTMLDivElement;
  private readonly styleEl: HTMLStyleElement;
  private previousState: GameState = GameState.MAIN_MENU;

  constructor() {
    this.root = document.createElement("div");
    this.root.id = "ui-root";
    document.body.appendChild(this.root);

    this.styleEl = document.createElement("style");
    this.styleEl.textContent = `
      #ui-root {
        position: absolute;
        inset: 0;
        z-index: 20;
        pointer-events: none;
        font-family: monospace;
      }
      .ui-overlay {
        position: absolute;
        inset: 0;
        display: flex;
        align-items: center;
        justify-content: center;
        background: rgba(0, 0, 0, 0.7);
        pointer-events: auto;
      }
      .ui-panel {
        width: min(360px, calc(100vw - 48px));
        background: #c6c6c6;
        border: 2px solid #ffffff;
        border-right-color: #555555;
        border-bottom-color: #555555;
        box-shadow: 0 16px 40px rgba(0,0,0,0.35);
        padding: 20px;
        color: #404040;
      }
      .ui-title {
        margin: 0 0 20px;
        text-align: center;
        font-size: 24px;
        text-shadow: 2px 2px #ffffff;
      }
      .ui-btn {
        display: block;
        width: 100%;
        margin: 0 0 10px;
        padding: 10px 12px;
        background: #8b8b8b;
        border: 2px solid #555555;
        border-right-color: #ffffff;
        border-bottom-color: #ffffff;
        color: #ffffff;
        font: inherit;
        text-shadow: 1px 1px #000000;
        cursor: pointer;
      }
      .ui-btn:hover {
        background: #a1a1a1;
      }
      .ui-label {
        display: block;
        margin-bottom: 8px;
      }
      .ui-slider {
        width: 100%;
        margin-bottom: 14px;
      }
    `;
    document.head.appendChild(this.styleEl);
    this.root.addEventListener("click", this.onDelegatedClick);
  }

  dispose(): void {
    this.root.removeEventListener("click", this.onDelegatedClick);
    this.styleEl.remove();
    this.root.remove();
  }

  handleAction(action: string): void {
    switch (action) {
      case "start-singleplayer":
        GameContext.state = GameState.GENERATING_WORLD;
        this.showLoadingScreen();
        break;
      case "show-options":
        this.previousState = GameContext.state;
        this.showOptionsMenu(this.previousState);
        break;
      case "save-render-distance": {
        const slider = document.getElementById("rd-slider") as HTMLInputElement | null;
        if (slider) GameContext.renderDistance = Number.parseInt(slider.value, 10) || GameContext.renderDistance;
        if (this.previousState === GameState.PAUSED) {
          this.showPauseMenu();
        } else {
          this.showMainMenu();
        }
        break;
      }
      case "resume-game":
        GameContext.state = GameState.IN_GAME;
        this.clearUI();
        break;
      case "quit-to-title":
        GameContext.state = GameState.MAIN_MENU;
        document.exitPointerLock?.();
        this.showMainMenu();
        break;
      case "toggle-pause":
        if (GameContext.state === GameState.IN_GAME) {
          GameContext.state = GameState.PAUSED;
          document.exitPointerLock?.();
          this.showPauseMenu();
        } else if (GameContext.state === GameState.PAUSED) {
          GameContext.state = GameState.IN_GAME;
          this.clearUI();
        }
        break;
      case "quit-game":
        window.location.reload();
        break;
      case "cancel-options":
        if (this.previousState === GameState.PAUSED) {
          this.showPauseMenu();
        } else {
          this.showMainMenu();
        }
        break;
    }
  }

  clearUI(): void {
    this.root.innerHTML = "";
  }

  showMainMenu(): void {
    GameContext.state = GameState.MAIN_MENU;
    this.clearUI();
    this.root.innerHTML = `
      <div class="ui-overlay">
        <div class="ui-panel">
          <div class="ui-title">VOXEL ENGINE (v1.5.2)</div>
          <button class="ui-btn" data-action="start-singleplayer">Singleplayer</button>
          <button class="ui-btn" data-action="show-options">Options</button>
          <button class="ui-btn" data-action="quit-game">Quit Game</button>
        </div>
      </div>
    `;
  }

  showPauseMenu(): void {
    this.clearUI();
    this.root.innerHTML = `
      <div class="ui-overlay">
        <div class="ui-panel">
          <div class="ui-title">Game Paused</div>
          <button class="ui-btn" data-action="resume-game">Back to Game</button>
          <button class="ui-btn" data-action="show-options">Options</button>
          <button class="ui-btn" data-action="quit-to-title">Save and Quit to Title</button>
        </div>
      </div>
    `;
  }

  showOptionsMenu(prevState: GameState): void {
    this.previousState = prevState;
    this.clearUI();
    this.root.innerHTML = `
      <div class="ui-overlay">
        <div class="ui-panel">
          <div class="ui-title">Options</div>
          <label class="ui-label">Render Distance: <span id="rd-val">${GameContext.renderDistance}</span> chunks</label>
          <input
            class="ui-slider"
            id="rd-slider"
            min="2"
            max="32"
            type="range"
            value="${GameContext.renderDistance}"
          />
          <button class="ui-btn" data-action="save-render-distance">Done</button>
          <button class="ui-btn" data-action="cancel-options">Cancel</button>
        </div>
      </div>
    `;
    const slider = document.getElementById("rd-slider") as HTMLInputElement | null;
    const readout = document.getElementById("rd-val");
    if (slider && readout) {
      slider.addEventListener("input", () => {
        readout.textContent = slider.value;
      }, { passive: true });
    }
  }

  showLoadingScreen(): void {
    this.clearUI();
    this.root.innerHTML = `
      <div class="ui-overlay">
        <div class="ui-panel">
          <div class="ui-title">Generating World...</div>
          <div>Building Terrain & Structures</div>
        </div>
      </div>
    `;
  }

  onWorldReady(): void {
    if (GameContext.state !== GameState.GENERATING_WORLD) return;
    GameContext.state = GameState.IN_GAME;
    this.clearUI();
  }

  private readonly onDelegatedClick = (event: MouseEvent): void => {
    const target = event.target as HTMLElement | null;
    const actionEl = target?.closest("[data-action]") as HTMLElement | null;
    if (!actionEl) return;
    const action = actionEl.dataset.action;
    if (action) this.handleAction(action);
  };
}
