import type { GameConfig } from "../engine/core/Config.js";
import { GameContext, GameState } from "../engine/core/GameState.js";
import { GameLoop } from "../engine/core/GameLoop.js";
import { InputState } from "../engine/core/InputState.js";
import { SharedPool } from "../engine/alloc/SharedPool.js";
import { EntityManager } from "../engine/ecs/EntityManager.js";
import { ComponentStore } from "../engine/ecs/ComponentStore.js";
import { SystemManager } from "../engine/ecs/SystemManager.js";
import { TransformDesc } from "../engine/ecs/components/Transform.js";
import { RigidBodyDesc } from "../engine/ecs/components/RigidBody.js";
import { AIStateDesc } from "../engine/ecs/components/AIState.js";
import { HealthDesc } from "../engine/ecs/components/Health.js";
import { MobStatsDesc } from "../engine/ecs/components/MobStats.js";
import { AudioEmitterDesc } from "../engine/ecs/components/AudioEmitter.js";
import { HostileTagDesc, FriendlyTagDesc } from "../engine/ecs/components/Tags.js";
import { PlayerComponentDesc } from "../engine/ecs/components/PlayerComponent.js";
import { InventoryComponentDesc } from "../engine/ecs/components/InventoryComponent.js";
import { PlayerControllerSystem } from "../engine/ecs/systems/PlayerControllerSystem.js";
import { PhysicsSystem } from "../engine/ecs/systems/PhysicsSystem.js";
import { PathfindingSystem } from "../engine/ecs/systems/PathfindingSystem.js";
import { AISystem } from "../engine/ecs/systems/AISystem.js";
import { HealthSystem } from "../engine/ecs/systems/HealthSystem.js";
import { MobRenderSystem } from "../engine/ecs/systems/MobRenderSystem.js";
import { CameraSystem } from "../engine/ecs/systems/CameraSystem.js";
import { TimeSystem } from "../engine/ecs/systems/TimeSystem.js";
import { AudioSystem } from "../engine/ecs/systems/AudioSystem.js";
import { RedstoneSystem } from "../engine/ecs/systems/RedstoneSystem.js";
import { ParticleSystem } from "../engine/ecs/systems/ParticleSystem.js";
import { AudioNodePool } from "../engine/audio/AudioNodePool.js";
import { BlockRegistry } from "../world/BlockRegistry.js";
import { VanillaBlockFactory } from "../world/BlockFactory.js";
import { World } from "../world/World.js";
import { spawnWorkers } from "../engine/workers/WorkerSpawner.js";
import { Renderer } from "../engine/render/Renderer.js";
import { MobFactory, MobType } from "../content/mobs/MobFactory.js";
import { bootstrapPlayerControls } from "./PlayerBootstrap.js";
import { UIManager } from "../ui/UIManager.js";
import { InventoryHud } from "../ui/InventoryHud.js";
import { InventoryControllerSystem, type InventoryAction } from "./inventory/InventoryControllerSystem.js";
import { CraftingSystem } from "./inventory/CraftingSystem.js";
import { createDefaultCraftingRegistry } from "../content/crafting/CraftingRegistry.js";
import type { CursorItem } from "./inventory/CursorItem.js";
import { AudioRegistry } from "../content/audio/AudioRegistry.js";
import { SoundId } from "../content/audio/AudioRegistry.js";
import { BlockInteractionAudio } from "./BlockInteractionAudio.js";
import { SaveManager } from "../engine/save/SaveManager.js";

export class Game {
  private readonly gl: WebGL2RenderingContext;
  private readonly renderer: Renderer;
  private readonly world: World;
  private readonly input = new InputState();
  private readonly disposePlayerControls: () => void;
  private readonly loop: GameLoop;
  private readonly systems = new SystemManager<Game>();
  private readonly ui = new UIManager();
  private readonly hud: InventoryHud;
  private readonly inventoryController = new InventoryControllerSystem();
  private readonly craftingSystem = new CraftingSystem(createDefaultCraftingRegistry());
  private readonly cursor: CursorItem = { itemId: 0, count: 0, metadata: 0 };
  private readonly entityManager = new EntityManager(1 << 12);
  private readonly transforms = new ComponentStore(TransformDesc, this.entityManager.capacity);
  private readonly bodies = new ComponentStore(RigidBodyDesc, this.entityManager.capacity);
  private readonly aiStates = new ComponentStore(AIStateDesc, this.entityManager.capacity);
  private readonly health = new ComponentStore(HealthDesc, this.entityManager.capacity);
  private readonly mobStats = new ComponentStore(MobStatsDesc, this.entityManager.capacity);
  private readonly emitters = new ComponentStore(AudioEmitterDesc, this.entityManager.capacity);
  private readonly hostileTags = new ComponentStore(HostileTagDesc, this.entityManager.capacity);
  private readonly friendlyTags = new ComponentStore(FriendlyTagDesc, this.entityManager.capacity);
  private readonly players = new ComponentStore(PlayerComponentDesc, this.entityManager.capacity);
  private readonly inventories = new ComponentStore(InventoryComponentDesc, this.entityManager.capacity);
  private readonly cameraSystem = new CameraSystem(this.transforms, this.players);
  private readonly timeSystem: TimeSystem;
  private readonly saveManager: SaveManager;
  private readonly audioContext: AudioContext;
  private readonly audioRegistry: AudioRegistry;
  private readonly audioPool: AudioNodePool;
  private readonly audioSystem: AudioSystem<Game>;
  private readonly redstoneSystem: RedstoneSystem<Game>;
  private readonly particleSystem: ParticleSystem<Game>;
  private readonly blockAudio: BlockInteractionAudio;
  private readonly playerEntityId: number;
  private readonly playerEntityIndex: number;
  private lastPrimaryMouseDown = false;
  private lastSecondaryMouseDown = false;

  constructor(
    private readonly config: GameConfig,
    private readonly canvas: HTMLCanvasElement,
  ) {
    const gl = canvas.getContext("webgl2", {
      alpha: false,
      antialias: false,
      powerPreference: "high-performance",
    });
    if (!gl) throw new Error("WebGL2 is required");
    if (typeof SharedArrayBuffer === "undefined") {
      throw new Error("SharedArrayBuffer is unavailable. Serve with COOP/COEP headers.");
    }

    this.gl = gl;
    GameContext.renderDistance = config.renderDistance;
    GameContext.worldSeed = config.worldSeed;
    this.hud = new InventoryHud(this.handleInventoryAction);

    const blocks = new BlockRegistry(4096);
    new VanillaBlockFactory().registerAll(blocks);

    const pool = SharedPool.create((config.renderDistance * 2 + 1) ** 2 + 8, {
      sizeX: config.chunkSize,
      sizeY: config.worldHeight,
      sizeZ: config.chunkSize,
      maxVertsPerChunk: config.maxVertsPerChunk,
      maxIndicesPerChunk: config.maxIndicesPerChunk,
      vertexStrideFloats: 10,
    });

    const worldGenWorkers = spawnWorkers("worldgen", config.maxConcurrentGenJobs, pool.bootstrap(), config.worldSeed);
    const mesherWorkers = spawnWorkers("mesher", config.maxConcurrentMeshJobs, pool.bootstrap(), config.worldSeed);

    this.world = new World(pool, worldGenWorkers, mesherWorkers, blocks, config);
    this.renderer = new Renderer(gl, blocks, config);
    this.timeSystem = new TimeSystem(this.renderer.getTimeUbo());
    const saveWorker = new Worker(new URL("../engine/workers/SaveWorker.js", import.meta.url), { type: "module" });
    this.saveManager = new SaveManager(saveWorker, pool, this.world);
    this.world.attachSaveManager(this.saveManager);
    this.audioContext = new AudioContext();
    this.audioRegistry = new AudioRegistry();
    this.audioRegistry.seedBuiltinSounds(this.audioContext);
    this.audioPool = new AudioNodePool(this.audioContext);
    this.redstoneSystem = new RedstoneSystem(pool, this.world);
    this.particleSystem = new ParticleSystem(this.renderer, blocks);
    this.audioSystem = new AudioSystem(
      this.transforms,
      this.emitters,
      this.cameraSystem,
      this.audioContext,
      this.audioPool,
      this.audioRegistry,
    );
    this.blockAudio = new BlockInteractionAudio(this.audioPool, this.audioRegistry, blocks);
    this.disposePlayerControls = bootstrapPlayerControls(canvas, this.input);

    this.playerEntityId = this.entityManager.allocate();
    this.playerEntityIndex = EntityManager.indexOf(this.playerEntityId);
    this.initializePlayer();
    this.seedPlayerInventory();

    const mobs = new MobFactory(
      this.entityManager,
      this.transforms,
      this.bodies,
      this.health,
      this.aiStates,
      this.mobStats,
      this.hostileTags,
      this.friendlyTags,
      this.emitters,
    );
    mobs.spawn(MobType.PIG, 8, 78, 8);
    mobs.spawn(MobType.SHEEP, 12, 79, 10);
    mobs.spawn(MobType.ZOMBIE, 20, 82, 16);

    this.systems.add(new PlayerControllerSystem(this.transforms, this.bodies, this.players, this.input));
    this.systems.add(new PhysicsSystem(this.entityManager, this.transforms, this.bodies, this.world));
    this.systems.add(new PathfindingSystem());
    this.systems.add(new AISystem());
    this.systems.add(new HealthSystem(this.health));
    this.systems.add(this.redstoneSystem);
    this.systems.add(this.particleSystem);
    this.systems.add(this.audioSystem);
    this.systems.add(new MobRenderSystem());

    this.loop = new GameLoop(
      config.targetFps,
      this.ui,
      this.input,
      (dt) => this.update(dt),
      (_alpha, timeSeconds) => this.render(timeSeconds),
    );
  }

  start(): void {
    this.loop.start();
  }

  dispose(): void {
    this.loop.stop();
    this.disposePlayerControls();
    this.world.dispose();
    this.saveManager.dispose();
    this.audioSystem.dispose();
    this.particleSystem.dispose();
    this.audioPool.dispose();
    void this.audioContext.close();
    this.renderer.dispose();
    this.ui.dispose();
    this.hud.dispose();
    void this.gl;
  }

  private initializePlayer(): void {
    const tRow = this.transforms.add(this.playerEntityIndex);
    this.transforms.data.position[tRow * 3 + 0] = this.config.chunkSize * 0.5;
    this.transforms.data.position[tRow * 3 + 1] = 80;
    this.transforms.data.position[tRow * 3 + 2] = this.config.chunkSize * 0.5;
    this.transforms.data.rotation[tRow * 4 + 3] = 1;
    this.transforms.data.scale[tRow * 3 + 0] = 1;
    this.transforms.data.scale[tRow * 3 + 1] = 1;
    this.transforms.data.scale[tRow * 3 + 2] = 1;

    const bRow = this.bodies.add(this.playerEntityIndex);
    this.bodies.data.aabbMin[bRow * 3 + 0] = -0.3;
    this.bodies.data.aabbMin[bRow * 3 + 1] = 0;
    this.bodies.data.aabbMin[bRow * 3 + 2] = -0.3;
    this.bodies.data.aabbMax[bRow * 3 + 0] = 0.3;
    this.bodies.data.aabbMax[bRow * 3 + 1] = 1.8;
    this.bodies.data.aabbMax[bRow * 3 + 2] = 0.3;
    this.bodies.data.drag[bRow] = 0.92;
    this.bodies.data.gravity[bRow] = 0;
    this.bodies.data.isFluid[bRow] = 0;
    this.bodies.data.onGround[bRow] = 0;

    const pRow = this.players.add(this.playerEntityIndex);
    this.players.data.yaw[pRow] = -Math.PI * 0.5;
    this.players.data.pitch[pRow] = -0.35;
    this.players.data.eyeHeight[pRow] = 1.62;
    this.players.data.walkSpeed[pRow] = 6;
    this.players.data.sprintSpeed[pRow] = 9;
    this.players.data.flySpeed[pRow] = 13;
    this.players.data.isFlying[pRow] = 1;
    this.players.data.selectedHotbarSlot[pRow] = 0;

    const hRow = this.health.add(this.playerEntityIndex);
    this.health.data.hp[hRow] = 20;
    this.health.data.maxHp[hRow] = 20;
    this.health.data.regenCd[hRow] = 0;

    const aRow = this.aiStates.add(this.playerEntityIndex);
    this.aiStates.data.targetEntity[aRow] = 0xffff_ffff;
    this.aiStates.data.pathHead[aRow] = 0;
    this.aiStates.data.pathLen[aRow] = 0;
    this.aiStates.data.state[aRow] = 0;
    this.aiStates.data.attackCd[aRow] = 0;

    const invRow = this.inventories.add(this.playerEntityIndex);
    this.inventories.data.itemIds.fill(0, invRow * 45, invRow * 45 + 45);
    this.inventories.data.itemCounts.fill(0, invRow * 45, invRow * 45 + 45);
    this.inventories.data.itemMetadata.fill(0, invRow * 45, invRow * 45 + 45);
  }

  private seedPlayerInventory(): void {
    const row = this.inventories.rowFor(this.playerEntityIndex);
    if (row === -1) return;
    const base = row * 45;
    const ids = this.inventories.data.itemIds;
    const counts = this.inventories.data.itemCounts;

    const starter: Array<[number, number, number]> = [
      [0, 17, 16],
      [1, 5, 32],
      [2, 4, 64],
      [3, 20, 16],
      [4, 12, 24],
      [5, 24, 12],
      [6, 58, 1],
      [7, 56, 8],
      [8, 49, 8],
    ];

    for (const [slot, itemId, count] of starter) {
      ids[base + slot] = itemId;
      counts[base + slot] = count;
    }
    this.craftingSystem.updatePlayerCraftingOutput(this.inventories, row);
  }

  private update(dt: number): void {
    this.config.renderDistance = GameContext.renderDistance;
    if (GameContext.state !== GameState.IN_GAME) {
      this.hud.setInventoryOpen(false);
    }

    this.syncHotbarSelection();

    if (GameContext.state === GameState.GENERATING_WORLD) {
      this.timeSystem.update(dt);
      this.cameraSystem.updateMatrices();
      this.world.update(this.cameraSystem.position);
      this.saveManager.update(dt);
      if (this.world.isReady()) this.ui.onWorldReady();
      return;
    }

    if (GameContext.state !== GameState.IN_GAME) return;

    if (this.input.isPressedCode("KeyE")) this.toggleInventory();

    if (this.hud.isOpen()) {
      this.stopPlayerMotion();
    } else {
      this.systems.update(this, dt);
    }

    if (this.input.pointerLocked) void this.audioPool.resume();
    this.timeSystem.update(dt);
    this.cameraSystem.updateMatrices();
    this.world.update(this.cameraSystem.position);
    this.saveManager.update(dt);
    this.handleDebugInteractions();
  }

  private render(timeSeconds: number): void {
    if (GameContext.state !== GameState.IN_GAME && this.hud.isOpen()) {
      this.hud.setInventoryOpen(false);
    }
    this.renderer.render(this.world, this.cameraSystem, timeSeconds, this.timeSystem.skyDarkness);
    this.particleSystem.render();
    const playerRow = this.players.rowFor(this.playerEntityIndex);
    const selected = playerRow === -1 ? 0 : this.players.data.selectedHotbarSlot[playerRow];
    this.hud.render(this.inventories, this.playerEntityIndex, this.cursor, selected, GameContext.state);
  }

  private toggleInventory(): void {
    const next = !this.hud.isOpen();
    this.hud.setInventoryOpen(next);
    if (next) {
      document.exitPointerLock?.();
      this.input.clearMovementState();
    }
  }

  private syncHotbarSelection(): void {
    const playerRow = this.players.rowFor(this.playerEntityIndex);
    if (playerRow === -1) return;

    for (let i = 0; i < 9; i++) {
      if (this.input.isPressedCode(`Digit${i + 1}`)) {
        this.players.data.selectedHotbarSlot[playerRow] = i;
      }
    }
  }

  private stopPlayerMotion(): void {
    const bodyRow = this.bodies.rowFor(this.playerEntityIndex);
    if (bodyRow === -1) return;
    this.bodies.data.velocity[bodyRow * 3 + 0] = 0;
    this.bodies.data.velocity[bodyRow * 3 + 1] = 0;
    this.bodies.data.velocity[bodyRow * 3 + 2] = 0;
  }

  private readonly handleInventoryAction = (action: InventoryAction): void => {
    if (GameContext.state !== GameState.IN_GAME) return;
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
    } else if (this.cursor.itemId === outputId && this.cursor.metadata === outputMeta && this.cursor.count + takeCount <= 64) {
      this.cursor.count += takeCount;
    } else {
      return;
    }

    this.craftingSystem.consumePlayerCraftingGrid(this.inventories, row);
  }

  private handleDebugInteractions(): void {
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

  private getDebugInteractionTarget(): { x: number; y: number; z: number; blockId: number } | null {
    const x = Math.floor(this.cameraSystem.position[0]);
    const z = Math.floor(this.cameraSystem.position[2]);
    const y = Math.max(0, Math.floor(this.cameraSystem.position[1] - 1.8));
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
