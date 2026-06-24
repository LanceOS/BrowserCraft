import type { GameConfig } from "../engine/core/Config.js";
import { GameState } from "../engine/core/GameState.js";
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
import { AudioRegistry } from "../content/audio/AudioRegistry.js";
import { BlockInteractionAudio } from "./BlockInteractionAudio.js";
import { SaveManager } from "../engine/save/SaveManager.js";
import { GameSession, MAX_RENDER_DISTANCE, type GameMode } from "./GameSession.js";
import { PlayerInteractionController } from "./PlayerInteractionController.js";

export interface GameOptions {
  readonly initialState?: GameState;
  readonly initialGameMode?: GameMode;
  readonly onStartSingleplayer?: (gameMode: GameMode) => void;
  readonly onQuitToTitle?: () => void;
  readonly saveSlotId?: string;
}

export class Game {
  private readonly gl: WebGL2RenderingContext;
  private readonly renderer: Renderer;
  private readonly world: World;
  private readonly input = new InputState();
  private readonly session: GameSession;
  private readonly ui: UIManager;
  private readonly disposePlayerControls: () => void;
  private readonly loop: GameLoop;
  private readonly systems = new SystemManager<Game>();
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
  private readonly playerInteractions: PlayerInteractionController;
  private readonly playerEntityId: number;
  private readonly playerEntityIndex: number;
  private appliedStartRequestId = 0;
  private placedPlayerAtWorldSpawn = false;
  private readonly initialState: GameState;
  private readonly initialGameMode: GameMode;

  constructor(
    private readonly config: GameConfig,
    private readonly canvas: HTMLCanvasElement,
    options: GameOptions = {},
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
    this.session = new GameSession(config.renderDistance);
    this.initialState = options.initialState ?? GameState.MAIN_MENU;
    this.initialGameMode = options.initialGameMode ?? "survival";

    const blocks = new BlockRegistry(4096);
    new VanillaBlockFactory().registerAll(blocks);

    const pool = SharedPool.create((MAX_RENDER_DISTANCE * 2 + 1) ** 2 + 8, {
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
    this.saveManager = new SaveManager(saveWorker, pool, this.world, options.saveSlotId ?? "default");
    this.world.attachSaveManager(this.saveManager);
    this.ui = new UIManager(this.session, {
      onStartSingleplayer: options.onStartSingleplayer,
      onQuitToTitle: options.onQuitToTitle ?? (() => this.saveManager.flushPending()),
    });
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

    this.playerEntityId = this.entityManager.allocate();
    this.playerEntityIndex = EntityManager.indexOf(this.playerEntityId);
    this.initializePlayer();
    this.cameraSystem.syncFromPlayer();
    this.playerInteractions = new PlayerInteractionController(
      this.input,
      this.inventories,
      this.players,
      this.bodies,
      this.world,
      blocks,
      this.blockAudio,
      this.particleSystem,
      this.redstoneSystem,
      this.audioRegistry,
      this.audioPool,
      this.cameraSystem,
      this.playerEntityIndex,
    );
    this.configurePlayerForMode(this.session.gameMode);
    this.appliedStartRequestId = this.session.startRequestId;
    this.disposePlayerControls = bootstrapPlayerControls(
      canvas,
      this.input,
      this.session,
      () => this.playerInteractions.toggleInventory(),
    );

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
    this.systems.add(new MobRenderSystem());

    this.loop = new GameLoop(
      config.targetFps,
      this.ui,
      this.input,
      this.session,
      (dt) => this.update(dt),
      (_alpha, timeSeconds) => this.render(timeSeconds),
    );
  }

  start(): void {
    if (this.initialState === GameState.GENERATING_WORLD) {
      this.session.startSingleplayer(this.initialGameMode);
      this.ui.showLoadingScreen(this.initialGameMode === "creative" ? "Creative" : "Survival");
    } else {
      this.session.enterMainMenu();
      this.ui.showMainMenu();
    }
    this.loop.start();
  }

  getRenderDistance(): number {
    return this.session.renderDistance;
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
    this.playerInteractions.dispose();
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

  private update(dt: number): void {
    const state = this.session.state;
    this.config.renderDistance = this.session.renderDistance;
    this.playerInteractions.syncState(state);
    this.playerInteractions.syncHotbarSelection();

    if (this.session.startRequestId !== this.appliedStartRequestId) {
      this.appliedStartRequestId = this.session.startRequestId;
      this.configurePlayerForMode(this.session.gameMode);
      this.cameraSystem.syncFromPlayer();
    }

    if (state === GameState.GENERATING_WORLD) {
      this.timeSystem.update(dt);
      this.cameraSystem.syncFromPlayer();
      this.world.update(this.cameraSystem.position);
      this.saveManager.update(dt);
      if (this.world.isReady()) {
        this.placePlayerAtWorldSpawn();
        this.cameraSystem.syncFromPlayer();
        this.ui.onWorldReady();
      }
      return;
    }

    if (state !== GameState.IN_GAME) return;

    if (this.playerInteractions.isInventoryOpen()) {
      this.playerInteractions.stopPlayerMotion();
    } else {
      this.systems.update(this, dt);
    }

    this.cameraSystem.syncFromPlayer();
    if (this.input.pointerLocked) void this.audioPool.resume();
    this.timeSystem.update(dt);
    this.audioSystem.update(this, dt);
    this.world.update(this.cameraSystem.position);
    this.saveManager.update(dt);
    this.playerInteractions.handleDebugInteractions();
  }

  private render(timeSeconds: number): void {
    this.playerInteractions.syncState(this.session.state);
    const aspectRatio = this.renderer.resizeCanvasToDisplaySize();
    this.cameraSystem.updateProjection(aspectRatio, this.config.fovDegrees);
    this.cameraSystem.updateMatrices();
    this.renderer.render(this.world, this.cameraSystem, timeSeconds, this.timeSystem.daylightFactor);
    this.particleSystem.render();
    this.playerInteractions.render(this.session.state);
  }

  private configurePlayerForMode(mode: GameMode): void {
    this.resetPlayerState(mode);
    this.resetPlayerInventory();
    if (mode === "creative") {
      this.seedCreativeInventory();
    }
    this.playerInteractions.refreshCraftingOutput();
  }

  private resetPlayerState(mode: GameMode): void {
    this.placedPlayerAtWorldSpawn = false;
    const transformRow = this.transforms.rowFor(this.playerEntityIndex);
    if (transformRow !== -1) {
      this.transforms.data.position[transformRow * 3 + 0] = this.config.chunkSize * 0.5;
      this.transforms.data.position[transformRow * 3 + 1] = 80;
      this.transforms.data.position[transformRow * 3 + 2] = this.config.chunkSize * 0.5;
    }

    const bodyRow = this.bodies.rowFor(this.playerEntityIndex);
    if (bodyRow !== -1) {
      this.bodies.data.velocity[bodyRow * 3 + 0] = 0;
      this.bodies.data.velocity[bodyRow * 3 + 1] = 0;
      this.bodies.data.velocity[bodyRow * 3 + 2] = 0;
      this.bodies.data.gravity[bodyRow] = mode === "creative" ? 0 : 20;
      this.bodies.data.onGround[bodyRow] = 0;
      this.bodies.data.isFluid[bodyRow] = 0;
    }

    const playerRow = this.players.rowFor(this.playerEntityIndex);
    if (playerRow !== -1) {
      this.players.data.isFlying[playerRow] = mode === "creative" ? 1 : 0;
      this.players.data.selectedHotbarSlot[playerRow] = 0;
    }

    const healthRow = this.health.rowFor(this.playerEntityIndex);
    if (healthRow !== -1) {
      this.health.data.hp[healthRow] = this.health.data.maxHp[healthRow];
      this.health.data.regenCd[healthRow] = 0;
    }
  }

  private placePlayerAtWorldSpawn(): void {
    if (this.placedPlayerAtWorldSpawn) return;
    const transformRow = this.transforms.rowFor(this.playerEntityIndex);
    if (transformRow === -1) return;

    const spawn = this.findSafeSpawnPosition();
    const spawnX = spawn?.x ?? this.config.chunkSize * 0.5;
    const spawnY = spawn?.y ?? 82;
    const spawnZ = spawn?.z ?? this.config.chunkSize * 0.5;
    const transformBase = transformRow * 3;
    this.transforms.data.position[transformBase + 0] = spawnX;
    this.transforms.data.position[transformBase + 1] = spawnY;
    this.transforms.data.position[transformBase + 2] = spawnZ;

    const bodyRow = this.bodies.rowFor(this.playerEntityIndex);
    if (bodyRow !== -1) {
      const bodyBase = bodyRow * 3;
      this.bodies.data.velocity[bodyBase + 0] = 0;
      this.bodies.data.velocity[bodyBase + 1] = 0;
      this.bodies.data.velocity[bodyBase + 2] = 0;
      this.bodies.data.onGround[bodyRow] = 0;
      this.bodies.data.isFluid[bodyRow] = 0;
    }

    this.placedPlayerAtWorldSpawn = true;
  }

  private findSafeSpawnPosition(): { x: number; y: number; z: number } | null {
    const centerX = Math.floor(this.config.chunkSize * 0.5);
    const centerZ = Math.floor(this.config.chunkSize * 0.5);
    const maxRadius = Math.max(
      centerX,
      centerZ,
      this.config.chunkSize - centerX - 1,
      this.config.chunkSize - centerZ - 1,
    );
    let waterCandidate: { x: number; y: number; z: number } | null = null;

    for (let radius = 0; radius <= maxRadius; radius++) {
      for (let dz = -radius; dz <= radius; dz++) {
        for (let dx = -radius; dx <= radius; dx++) {
          if (Math.max(Math.abs(dx), Math.abs(dz)) !== radius) continue;
          const worldX = centerX + dx;
          const worldZ = centerZ + dz;
          if (worldX < 0 || worldX >= this.config.chunkSize || worldZ < 0 || worldZ >= this.config.chunkSize) {
            continue;
          }

          const candidate = this.findSpawnCandidate(worldX, worldZ);
          if (!candidate) continue;
          const spawn = { x: worldX + 0.5, y: candidate.y, z: worldZ + 0.5 };
          if (candidate.isDry) return spawn;
          waterCandidate ??= spawn;
        }
      }
    }

    return waterCandidate;
  }

  private findSpawnCandidate(worldX: number, worldZ: number): { y: number; isDry: boolean } | null {
    for (let y = this.config.worldHeight - 2; y >= 0; y--) {
      if (!this.world.isSolid(worldX, y, worldZ)) continue;
      let spawnY = y + 1;
      let skippedFluid = false;
      while (spawnY < this.config.worldHeight - 2 && this.world.isFluid(worldX, spawnY, worldZ)) {
        skippedFluid = true;
        spawnY++;
      }
      return { y: spawnY + 0.05, isDry: !skippedFluid };
    }

    return null;
  }

  private resetPlayerInventory(): void {
    const row = this.inventories.rowFor(this.playerEntityIndex);
    if (row === -1) return;
    this.inventories.data.itemIds.fill(0, row * 45, row * 45 + 45);
    this.inventories.data.itemCounts.fill(0, row * 45, row * 45 + 45);
    this.inventories.data.itemMetadata.fill(0, row * 45, row * 45 + 45);
  }

  private seedCreativeInventory(): void {
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
  }
}
