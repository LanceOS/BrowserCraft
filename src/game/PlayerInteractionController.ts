import { GameState } from "../engine/core/GameState.js";
import { InputState } from "../engine/core/InputState.js";
import type { ComponentStore } from "../engine/ecs/ComponentStore.js";
import { InventoryComponentDesc } from "../engine/ecs/components/InventoryComponent.js";
import { PlayerComponentDesc } from "../engine/ecs/components/PlayerComponent.js";
import { RigidBodyDesc } from "../engine/ecs/components/RigidBody.js";
import { ParticleSystem } from "../engine/ecs/systems/ParticleSystem.js";
import { RedstoneSystem } from "../engine/ecs/systems/RedstoneSystem.js";
import { AudioNodePool } from "../engine/audio/AudioNodePool.js";
import type { AudioListenerView } from "../engine/render/CameraView.js";
import { AudioRegistry, SoundId } from "../content/audio/AudioRegistry.js";
import { createDefaultCraftingRegistry } from "../content/crafting/CraftingRegistry.js";
import { World } from "../world/World.js";
import { InventoryHud } from "../ui/InventoryHud.js";
import { BlockInteractionAudio } from "./BlockInteractionAudio.js";
import { CraftingSystem } from "./inventory/CraftingSystem.js";
import type { CursorItem } from "./inventory/CursorItem.js";
import { InventoryControllerSystem, type InventoryAction } from "./inventory/InventoryControllerSystem.js";

export class PlayerInteractionController {
  private readonly hud: InventoryHud;
  private readonly inventoryController = new InventoryControllerSystem();
  private readonly craftingSystem = new CraftingSystem(createDefaultCraftingRegistry());
  private readonly cursor: CursorItem = { itemId: 0, count: 0, metadata: 0 };
  private lastPrimaryMouseDown = false;
  private lastSecondaryMouseDown = false;

  constructor(
    private readonly input: InputState,
    private readonly inventories: ComponentStore<typeof InventoryComponentDesc>,
    private readonly players: ComponentStore<typeof PlayerComponentDesc>,
    private readonly bodies: ComponentStore<typeof RigidBodyDesc>,
    private readonly world: World,
    private readonly blockAudio: BlockInteractionAudio,
    private readonly particleSystem: ParticleSystem<unknown>,
    private readonly redstoneSystem: RedstoneSystem<unknown>,
    private readonly audioRegistry: AudioRegistry,
    private readonly audioPool: AudioNodePool,
    private readonly camera: AudioListenerView,
    private readonly playerEntityIndex: number,
  ) {
    this.hud = new InventoryHud(this.handleInventoryAction);
  }

  dispose(): void {
    this.hud.dispose();
  }

  syncState(gameState: GameState): void {
    if (gameState !== GameState.IN_GAME) {
      this.hud.setInventoryOpen(false);
    }
  }

  refreshCraftingOutput(): void {
    const row = this.inventories.rowFor(this.playerEntityIndex);
    if (row === -1) return;
    this.craftingSystem.updatePlayerCraftingOutput(this.inventories, row);
  }

  render(gameState: GameState): void {
    const playerRow = this.players.rowFor(this.playerEntityIndex);
    const selectedHotbarSlot = playerRow === -1 ? 0 : this.players.data.selectedHotbarSlot[playerRow];
    this.hud.render(this.inventories, this.playerEntityIndex, this.cursor, selectedHotbarSlot, gameState);
  }

  syncHotbarSelection(): void {
    const playerRow = this.players.rowFor(this.playerEntityIndex);
    if (playerRow === -1) return;

    for (let i = 0; i < 9; i++) {
      if (this.input.isPressedCode(`Digit${i + 1}`)) {
        this.players.data.selectedHotbarSlot[playerRow] = i;
      }
    }
  }

  toggleInventory(): void {
    const next = !this.hud.isOpen();
    this.hud.setInventoryOpen(next);
    if (next) {
      document.exitPointerLock?.();
      this.input.clearMovementState();
    }
  }

  isInventoryOpen(): boolean {
    return this.hud.isOpen();
  }

  stopPlayerMotion(): void {
    const bodyRow = this.bodies.rowFor(this.playerEntityIndex);
    if (bodyRow === -1) return;
    this.bodies.data.velocity[bodyRow * 3 + 0] = 0;
    this.bodies.data.velocity[bodyRow * 3 + 1] = 0;
    this.bodies.data.velocity[bodyRow * 3 + 2] = 0;
  }

  handleDebugInteractions(): void {
    const target = this.getDebugInteractionTarget();
    const mouseDown = this.input.mouseButtons[0] === 1;
    if (mouseDown && !this.lastPrimaryMouseDown && this.input.pointerLocked && target) {
      this.blockAudio.onBlockBroken(target.x, target.y, target.z, target.blockId);
      this.particleSystem.spawnBlockBreak(target.x, target.y, target.z, target.blockId);
    }
    this.lastPrimaryMouseDown = mouseDown;

    const secondaryDown = this.input.mouseButtons[2] === 1;
    if (secondaryDown && !this.lastSecondaryMouseDown && this.input.pointerLocked) {
      this.toggleDebugRedstoneRig();
    }
    this.lastSecondaryMouseDown = secondaryDown;
  }

  private readonly handleInventoryAction = (action: InventoryAction): void => {
    if (!this.hud.isOpen()) return;

    const row = this.inventories.rowFor(this.playerEntityIndex);
    if (row === -1) return;

    if (action.slotIndex === 44) {
      this.takeCraftingOutput(row, action);
      return;
    }

    this.inventoryController.handleAction(this.inventories, this.playerEntityIndex, action, this.cursor);
    if (action.slotIndex >= 40 && action.slotIndex <= 43) {
      this.craftingSystem.updatePlayerCraftingOutput(this.inventories, row);
    }
  };

  private takeCraftingOutput(row: number, action: InventoryAction): void {
    const base = row * 45 + 44;
    const ids = this.inventories.data.itemIds;
    const counts = this.inventories.data.itemCounts;
    const meta = this.inventories.data.itemMetadata;
    const outputId = ids[base];
    const outputCount = counts[base];
    const outputMeta = meta[base];
    if (outputId === 0 || outputCount === 0) return;

    const takeCount = action.button === "right" ? 1 : outputCount;
    if (this.cursor.itemId === 0) {
      this.cursor.itemId = outputId;
      this.cursor.count = takeCount;
      this.cursor.metadata = outputMeta;
    } else if (
      this.cursor.itemId === outputId &&
      this.cursor.metadata === outputMeta &&
      this.cursor.count + takeCount <= 64
    ) {
      this.cursor.count += takeCount;
    } else {
      return;
    }

    this.craftingSystem.consumePlayerCraftingGrid(this.inventories, row);
  }

  private getDebugInteractionTarget(): { x: number; y: number; z: number; blockId: number } | null {
    const x = Math.floor(this.camera.position[0]);
    const z = Math.floor(this.camera.position[2]);
    const y = Math.max(0, Math.floor(this.camera.position[1] - 1.8));
    const blockId = this.world.getBlockIdAt(x, y, z) || this.world.getBlockIdAt(x, y - 1, z);
    if (blockId === 0) return null;
    return { x, y, z, blockId };
  }

  private toggleDebugRedstoneRig(): void {
    const target = this.getDebugInteractionTarget();
    if (!target) return;

    const baseY = target.y + 1;
    const torchX = target.x - 1;
    const wireX = target.x;
    const lampX = target.x + 1;

    const torchBlock = this.world.getBlockIdAt(torchX, baseY, target.z);
    const wireBlock = this.world.getBlockIdAt(wireX, baseY, target.z);
    const lampBlock = this.world.getBlockIdAt(lampX, baseY, target.z);
    const hasRig = (torchBlock === 75 || torchBlock === 76) && wireBlock === 55 && lampBlock === 123;

    if (!hasRig) {
      this.world.setBlockIdAt(torchX, baseY, target.z, 76);
      this.world.setBlockIdAt(wireX, baseY, target.z, 55);
      this.world.setBlockIdAt(lampX, baseY, target.z, 123);
      this.world.setRedstonePackedAt(torchX, baseY, target.z, 15);
      this.world.setRedstonePackedAt(wireX, baseY, target.z, 0);
      this.world.setRedstonePackedAt(lampX, baseY, target.z, 0);
      this.redstoneSystem.triggerAtWorld(torchX, baseY, target.z, 15);
    } else {
      const nextOn = torchBlock !== 76;
      this.world.setBlockIdAt(torchX, baseY, target.z, nextOn ? 76 : 75);
      this.world.setRedstonePackedAt(torchX, baseY, target.z, nextOn ? 15 : 0);
      this.redstoneSystem.triggerAtWorld(torchX, baseY, target.z, nextOn ? 15 : 0);
    }

    const click = this.audioRegistry.get(SoundId.CLICK);
    if (click) this.audioPool.playUI(click, 0.25);
  }
}
