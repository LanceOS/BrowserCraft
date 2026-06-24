import { GameState } from "../engine/core/GameState.js";
import type { ComponentStore } from "../engine/ecs/ComponentStore.js";
import { InventoryComponentDesc } from "../engine/ecs/components/InventoryComponent.js";
import type { CursorItem } from "../game/inventory/CursorItem.js";
import type { InventoryAction } from "../game/inventory/InventoryControllerSystem.js";

export type ItemNameResolver = (itemId: number) => string | undefined;

export class InventoryHud {
  private readonly root: HTMLDivElement;
  private readonly styleEl: HTMLStyleElement;
  private readonly hotbarSlots: HTMLDivElement[] = [];
  private readonly inventorySlots: HTMLDivElement[] = [];
  private readonly cursorLabel: HTMLDivElement;
  private readonly hotbar: HTMLDivElement;
  private readonly inventoryPanel: HTMLDivElement;
  private inventoryOpen = false;

  constructor(
    private readonly onAction: (action: InventoryAction) => void,
    private readonly resolveItemName: ItemNameResolver = () => undefined,
  ) {
    this.root = document.createElement("div");
    this.root.id = "hud-root";
    this.root.innerHTML = `
      <div class="hud-hotbar" data-hotbar></div>
      <div class="hud-inventory" data-inventory hidden></div>
      <div class="hud-cursor" data-cursor hidden></div>
    `;
    document.body.appendChild(this.root);

    this.styleEl = document.createElement("style");
    this.styleEl.textContent = `
      #hud-root {
        position: absolute;
        inset: 0;
        z-index: 12;
        pointer-events: none;
        font-family: monospace;
        color: #ffffff;
      }
      #hud-root[hidden],
      .hud-hotbar[hidden],
      .hud-inventory[hidden],
      .hud-cursor[hidden] {
        display: none !important;
      }
      .hud-hotbar {
        position: absolute;
        left: 50%;
        bottom: 18px;
        transform: translateX(-50%);
        display: grid;
        grid-template-columns: repeat(9, 48px);
        gap: 6px;
        pointer-events: auto;
      }
      .hud-inventory {
        position: absolute;
        left: 50%;
        top: 50%;
        transform: translate(-50%, -50%);
        width: min(640px, calc(100vw - 40px));
        background: rgba(34, 28, 20, 0.92);
        border: 3px solid #d0d0d0;
        box-shadow: 0 18px 50px rgba(0,0,0,0.45);
        padding: 16px;
        display: grid;
        grid-template-columns: repeat(9, minmax(46px, 1fr));
        gap: 8px;
        pointer-events: auto;
      }
      .hud-slot {
        aspect-ratio: 1;
        border: 2px solid #777;
        border-right-color: #ececec;
        border-bottom-color: #ececec;
        background: rgba(160, 160, 160, 0.34);
        position: relative;
        display: flex;
        align-items: center;
        justify-content: center;
        user-select: none;
      }
      .hud-slot[data-selected="true"] {
        outline: 3px solid #ffd85a;
      }
      .hud-slot-label {
        font-size: 9px;
        text-align: center;
        line-height: 1.1;
        padding: 2px;
        max-width: 100%;
        max-height: 3.3em;
        overflow: hidden;
        overflow-wrap: anywhere;
      }
      .hud-slot-count {
        position: absolute;
        right: 4px;
        bottom: 2px;
        font-size: 10px;
      }
      .hud-cursor {
        position: absolute;
        right: 18px;
        top: 18px;
        background: rgba(10, 10, 10, 0.72);
        border: 1px solid rgba(255, 255, 255, 0.24);
        padding: 8px 10px;
        pointer-events: none;
      }
    `;
    document.head.appendChild(this.styleEl);

    this.hotbar = this.root.querySelector("[data-hotbar]") as HTMLDivElement;
    this.inventoryPanel = this.root.querySelector("[data-inventory]") as HTMLDivElement;
    this.cursorLabel = this.root.querySelector("[data-cursor]") as HTMLDivElement;

    for (let slot = 0; slot < 9; slot++) {
      const el = this.createSlot(slot);
      this.hotbarSlots.push(el);
      this.hotbar.appendChild(el);
    }

    for (let slot = 0; slot < 45; slot++) {
      const el = this.createSlot(slot);
      this.inventorySlots.push(el);
      this.inventoryPanel.appendChild(el);
    }

    this.root.addEventListener("mousedown", this.onMouseDown);
    this.root.addEventListener("contextmenu", this.onContextMenu);
  }

  dispose(): void {
    this.root.removeEventListener("mousedown", this.onMouseDown);
    this.root.removeEventListener("contextmenu", this.onContextMenu);
    this.styleEl.remove();
    this.root.remove();
  }

  isOpen(): boolean {
    return this.inventoryOpen;
  }

  setInventoryOpen(open: boolean): void {
    this.inventoryOpen = open;
    this.inventoryPanel.hidden = !open;
  }

  render(
    inv: ComponentStore<typeof InventoryComponentDesc>,
    playerEntityIndex: number,
    cursor: CursorItem,
    selectedHotbarSlot: number,
    gameState: GameState,
  ): void {
    const row = inv.rowFor(playerEntityIndex);
    const visible = gameState === GameState.IN_GAME || gameState === GameState.PAUSED;
    this.root.hidden = !visible;
    this.hotbar.hidden = !visible;
    this.inventoryPanel.hidden = !visible || !this.inventoryOpen;
    if (!visible || row === -1) return;

    const ids = inv.data.itemIds;
    const counts = inv.data.itemCounts;
    const meta = inv.data.itemMetadata;
    const base = row * 45;

    for (let slot = 0; slot < 9; slot++) {
      this.paintSlot(this.hotbarSlots[slot], ids[base + slot], counts[base + slot], meta[base + slot], slot === selectedHotbarSlot);
    }

    for (let slot = 0; slot < 45; slot++) {
      this.paintSlot(this.inventorySlots[slot], ids[base + slot], counts[base + slot], meta[base + slot], slot === selectedHotbarSlot && slot < 9);
    }

    if (cursor.itemId !== 0 && cursor.count > 0) {
      this.cursorLabel.hidden = false;
      this.cursorLabel.textContent = `Cursor: ${this.displayNameFor(cursor.itemId)} x${cursor.count}`;
    } else {
      this.cursorLabel.hidden = true;
    }
  }

  private createSlot(slotIndex: number): HTMLDivElement {
    const slot = document.createElement("div");
    slot.className = "hud-slot";
    slot.dataset.slotIndex = String(slotIndex);
    slot.innerHTML = `<div class="hud-slot-label"></div><div class="hud-slot-count"></div>`;
    return slot;
  }

  private paintSlot(slot: HTMLDivElement, itemId: number, count: number, metadata: number, selected: boolean): void {
    slot.dataset.selected = selected ? "true" : "false";
    const label = slot.firstElementChild as HTMLDivElement;
    const countEl = slot.lastElementChild as HTMLDivElement;
    if (itemId === 0 || count === 0) {
      label.textContent = "";
      countEl.textContent = "";
      slot.title = "";
      return;
    }
    const name = this.displayNameFor(itemId);
    label.textContent = name;
    countEl.textContent = count > 1 ? String(count) : "";
    slot.title = `${name}${metadata ? `:${metadata}` : ""}`;
  }

  private displayNameFor(itemId: number): string {
    const rawName = this.resolveItemName(itemId);
    if (!rawName) return `Item ${itemId}`;
    return rawName
      .replace(/[_-]+/g, " ")
      .replace(/\b\w/g, (letter) => letter.toUpperCase());
  }

  private readonly onMouseDown = (event: MouseEvent): void => {
    const target = event.target as HTMLElement | null;
    const slotEl = target?.closest("[data-slot-index]") as HTMLElement | null;
    if (!slotEl) return;
    const slotIndex = Number.parseInt(slotEl.dataset.slotIndex ?? "-1", 10);
    if (slotIndex < 0) return;
    event.preventDefault();
    this.onAction({
      slotIndex,
      button: event.button === 2 ? "right" : "left",
      shiftHeld: event.shiftKey,
    });
  };

  private readonly onContextMenu = (event: MouseEvent): void => {
    if ((event.target as HTMLElement | null)?.closest("[data-slot-index]")) event.preventDefault();
  };
}
