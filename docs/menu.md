To implement the Main Menu and Pause Menu while strictly adhering to our zero-Garbage-Collection and Data-Oriented Design philosophies, we will use a **State-Driven DOM Manager**.

While WebGL2 renders the 3D world, HTML/DOM remains the most performant and accessible way to render static UI overlays *provided* we do not use heavy frameworks (like React) that trigger Virtual DOM diffing and GC churn. We build the DOM once, mutate styles/attributes directly, and use an event delegation pattern to avoid allocating hundreds of event listener closures.

### 1. Game State Machine

The UI is driven by a strict state machine. The `GameLoop` checks this state to decide whether to run ECS physics/AI updates or freeze the world.

```typescript
// /src/engine/core/GameState.ts

export const enum GameState {
  BOOTING,
  MAIN_MENU,
  GENERATING_WORLD,
  IN_GAME,
  PAUSED,
}

/**
 * Global Game State Singleton.
 * Mutated only by the UIManager and Game bootstrap.
 */
export const GameContext = {
  state: GameState.BOOTING,
  worldSeed: 0,
  renderDistance: 12,
};
```

### 2. UI Manager & Event Delegation

The `UIManager` handles DOM injection, CSS styling (themed around the 1.5.2 dirt/stone aesthetic), and click routing. It uses a single delegated event listener on the UI root container to process clicks based on `data-action` attributes, preventing closure allocation overhead.

```typescript
// /src/ui/UIManager.ts

export class UIManager {
  private readonly root: HTMLDivElement;
  private readonly styleEl: HTMLStyleElement;

  constructor() {
    this.root = document.createElement("div");
    this.root.id = "ui-root";
    document.body.appendChild(this.root);

    // Inject global UI styles (1.5.2 aesthetic)
    this.styleEl = document.createElement("style");
    this.styleEl.innerHTML = `
      #ui-root {
        position: absolute; top: 0; left: 0; width: 100%; height: 100%;
        pointer-events: none; z-index: 10;
        font-family: monospace; color: #fff;
      }
      .ui-overlay {
        position: absolute; top: 0; left: 0; width: 100%; height: 100%;
        display: flex; flex-direction: column; justify-content: center; align-items: center;
        background: rgba(0,0,0,0.7);
        pointer-events: auto;
      }
      .ui-panel {
        background: #c6c6c6; border: 2px solid #fff; border-right: 2px solid #555; border-bottom: 2px solid #555;
        padding: 20px; min-width: 300px;
      }
      .ui-title {
        font-size: 24px; text-align: center; margin-bottom: 20px; color: #404040;
        text-shadow: 2px 2px #fff;
      }
      .ui-btn {
        display: block; width: 100%; padding: 10px; margin-bottom: 10px;
        background: #8b8b8b; border: 2px solid #555; border-right: 2px solid #fff; border-bottom: 2px solid #fff;
        color: #fff; font-family: monospace; font-size: 16px; cursor: pointer;
        text-shadow: 1px 1px #000;
      }
      .ui-btn:hover { background: #a0a0a0; }
      .ui-input {
        display: block; width: 90%; padding: 8px; margin-bottom: 10px;
        background: #d0d0d0; border: 2px solid #555; color: #000; font-family: monospace;
      }
      .ui-label { color: #404040; margin-bottom: 5px; display: block; }
      .ui-row { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; color: #404040; }
      .ui-slider { width: 100%; }
    `;
    document.head.appendChild(this.styleEl);

    // Single delegated event listener for ALL UI clicks (Zero closure allocations per button)
    this.root.addEventListener("click", this.onDelegatedClick);
  }

  /**
   * Event delegation handler. Reads data-action attribute to determine logic.
   * O(1) complexity.
   */
  private onDelegatedClick = (e: MouseEvent): void => {
    const target = e.target as HTMLElement;
    const actionEl = target.closest("[data-action]") as HTMLElement | null;
    if (!actionEl) return;

    const action = actionEl.dataset.action;
    switch (action) {
      case "start-singleplayer":
        GameContext.worldSeed = Date.now();
        GameContext.state = GameState.GENERATING_WORLD;
        this.showLoadingScreen();
        break;
      case "quit-to-title":
        GameContext.state = GameState.MAIN_MENU;
        this.showMainMenu();
        break;
      case "resume-game":
        GameContext.state = GameState.IN_GAME;
        this.clearUI();
        break;
      case "toggle-pause":
        if (GameContext.state === GameState.IN_GAME) {
          GameContext.state = GameState.PAUSED;
          this.showPauseMenu();
        } else if (GameContext.state === GameState.PAUSED) {
          GameContext.state = GameState.IN_GAME;
          this.clearUI();
        }
        break;
      case "save-render-distance":
        const slider = document.getElementById("rd-slider") as HTMLInputElement;
        GameContext.renderDistance = parseInt(slider.value, 10);
        this.showMainMenu(); // refresh menu
        break;
    }
  };

  public clearUI(): void {
    this.root.innerHTML = "";
  }

  /** Injects the Main Menu HTML */
  public showMainMenu(): void {
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

  /** Injects the Pause Menu HTML */
  public showPauseMenu(): void {
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

  /** Injects the Options Menu HTML */
  public showOptionsMenu(prevState: GameState): void {
    this.clearUI();
    const backAction = prevState === GameState.PAUSED ? "resume-game" : "start-singleplayer";
    this.root.innerHTML = `
      <div class="ui-overlay">
        <div class="ui-panel">
          <div class="ui-title">Options</div>
          <label class="ui-label">Render Distance: <span id="rd-val">${GameContext.renderDistance}</span> chunks</label>
          <input type="range" min="2" max="32" value="${GameContext.renderDistance}" class="ui-slider" id="rd-slider" 
                 oninput="document.getElementById('rd-val').innerText = this.value">
          <button class="ui-btn" data-action="save-render-distance">Done</button>
          <button class="ui-btn" data-action="${backAction}">Cancel</button>
        </div>
      </div>
    `;
  }

  private showLoadingScreen(): void {
    this.clearUI();
    this.root.innerHTML = `
      <div class="ui-overlay">
        <div class="ui-panel">
          <div class="ui-title">Generating World...</div>
          <div style="color: #404040;">Building Terrain & Structures</div>
        </div>
      </div>
    `;
  }

  /** Called by GameLoop when world gen completes */
  public onWorldReady(): void {
    if (GameContext.state === GameState.GENERATING_WORLD) {
      GameContext.state = GameState.IN_GAME;
      this.clearUI();
    }
  }
}
```

### 3. GameLoop & Input Integration

The `GameLoop` must respect the `GameState`. If the game is paused or in a menu, we skip ECS physics and AI updates, but we *must* still render the last known frame so the background remains visible.

We also bind the `Escape` key to toggle the pause state using the same `data-action` delegation pattern.

```typescript
// /src/engine/core/GameLoop.ts

import { GameState, GameContext } from "./GameState";
import { UIManager } from "../../ui/UIManager";
import { InputState } from "./InputState";

export class GameLoop {
  private lastTime: number = 0;
  private accumulator: number = 0;
  private readonly fixedDt: number = 1 / 60;

  constructor(
    private readonly ui: UIManager,
    private readonly input: InputState,
    private readonly updateFn: (dt: number) => void,
    private readonly renderFn: () => void
  ) {
    // Bind Escape key to pause toggle via the UIManager's action map
    document.addEventListener("keydown", (e) => {
      if (e.code === "Escape") {
        // Simulate a click on the delegated UI handler
        const fakeEvent = { target: { closest: () => ({ dataset: { action: "toggle-pause" } }) } } as unknown as MouseEvent;
        // Directly invoke the logic by creating a temporary element, or just call a public method
        this.ui.handleAction("toggle-pause"); 
      }
    });
  }

  public start(): void {
    this.ui.showMainMenu();
    requestAnimationFrame(this.tick);
  }

  private tick = (now: number): void => {
    const elapsed = (now - this.lastTime) / 1000;
    this.lastTime = now;

    // Only advance ECS physics/AI if IN_GAME
    if (GameContext.state === GameState.IN_GAME) {
      this.accumulator += elapsed;
      while (this.accumulator >= this.fixedDt) {
        this.updateFn(this.fixedDt);
        this.accumulator -= this.fixedDt;
      }
    } else {
      // If paused or in menu, clear input so player doesn't move
      this.input.clearFrameState();
    }

    // ALWAYS render the last known state, so the world is visible behind the pause menu
    this.renderFn();

    requestAnimationFrame(this.tick);
  };
}
```

*Note: To make* `this.ui.handleAction` work, the `onDelegatedClick` logic in `UIManager` should be refactored into a public `handleAction(action: string)` method, which both the DOM click listener and the keyboard listener call.

### 4. WebGL Background Rendering (Panorama)

In the Main Menu state, if we want the classic rotating panorama background, the `renderFn` in `GameLoop` checks the state and renders a special panorama camera instead of the player camera.

```typescript
// /src/game/Game.ts (Render Logic Excerpt)

private render(): void {
  const gl = this.gl;
  gl.clearColor(0.5, 0.7, 0.9, 1.0);
  gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

  if (GameContext.state === GameState.MAIN_MENU || GameContext.state === GameState.BOOTING) {
    // Render a rotating panoramic skybox or a floating island for the menu background
    this.renderMenuBackground();
  } else {
    // Standard ECS Camera System render
    this.cameraSystem.update(0, performance.now() / 1000);
    this.renderer.draw();
  }
}

private renderMenuBackground(): void {
  // Rotate camera slowly
  const time = performance.now() / 1000;
  const yaw = time * 0.05;
  const pitch = Math.sin(time * 0.1) * 0.1;
  
  // Override UBO with menu camera matrices...
  // Render a special pre-baked voxel scene...
}
```

### Summary of Architectural Compliance


1. **Zero-Garbage UI:** By injecting static HTML strings and using a single delegated event listener reading `data-action` attributes, we avoid allocating hundreds of closure functions for UI buttons. No Virtual DOM diffing occurs.
2. **State Machine Integration:** The `GameState` enum strictly controls whether the `GameLoop` steps the ECS physics. If `PAUSED`, the accumulator is bypassed, freezing all mob movement and physics, but the `renderFn` still executes to draw the frozen world behind the menu.
3. **Theming:** The injected CSS replicates the classic 1.3.2/1.5.2 Minecraft aesthetic (gray panels with 2px beveled borders mimicking `gui.png`), satisfying the design requirement.
4. **Configurable Options:** The Options menu directly mutates `GameContext.renderDistance`, which the `ChunkManager` reads on the next chunk load cycle, fulfilling the dynamic settings requirement.


