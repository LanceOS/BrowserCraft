import { EntityManager } from "../../engine/ecs/EntityManager.js";
import { ComponentStore } from "../../engine/ecs/ComponentStore.js";
import { TransformDesc } from "../../engine/ecs/components/Transform.js";
import { RigidBodyDesc } from "../../engine/ecs/components/RigidBody.js";
import { HealthDesc } from "../../engine/ecs/components/Health.js";
import { AIStateDesc } from "../../engine/ecs/components/AIState.js";
import { MobStatsDesc } from "../../engine/ecs/components/MobStats.js";
import { HostileTagDesc, FriendlyTagDesc } from "../../engine/ecs/components/Tags.js";
import { AudioEmitterDesc } from "../../engine/ecs/components/AudioEmitter.js";

export const enum MobType {
  PIG,
  COW,
  SHEEP,
  CHICKEN,
  ZOMBIE,
  SKELETON,
  CREEPER,
  SPIDER,
}

interface MobConfig {
  readonly stats: {
    readonly width: number;
    readonly height: number;
    readonly eyeHeight: number;
    readonly moveSpeed: number;
    readonly attackDamage: number;
    readonly maxHealth: number;
  };
  readonly modelId: number;
  readonly hostile: boolean;
}

const MOB_CONFIGS: readonly MobConfig[] = [
  { stats: { width: 0.9, height: 0.9, eyeHeight: 0.8, moveSpeed: 4.0, attackDamage: 0, maxHealth: 10 }, modelId: 1, hostile: false },
  { stats: { width: 0.9, height: 1.2, eyeHeight: 1.1, moveSpeed: 4.0, attackDamage: 0, maxHealth: 10 }, modelId: 2, hostile: false },
  { stats: { width: 0.9, height: 1.3, eyeHeight: 1.2, moveSpeed: 4.0, attackDamage: 0, maxHealth: 8 }, modelId: 3, hostile: false },
  { stats: { width: 0.6, height: 0.8, eyeHeight: 0.7, moveSpeed: 4.0, attackDamage: 0, maxHealth: 4 }, modelId: 4, hostile: false },
  { stats: { width: 0.6, height: 1.9, eyeHeight: 1.7, moveSpeed: 4.3, attackDamage: 4, maxHealth: 20 }, modelId: 5, hostile: true },
  { stats: { width: 0.6, height: 1.9, eyeHeight: 1.7, moveSpeed: 5.0, attackDamage: 4, maxHealth: 20 }, modelId: 6, hostile: true },
  { stats: { width: 0.6, height: 1.7, eyeHeight: 1.5, moveSpeed: 4.5, attackDamage: 10, maxHealth: 20 }, modelId: 7, hostile: true },
  { stats: { width: 1.0, height: 0.8, eyeHeight: 0.6, moveSpeed: 6.0, attackDamage: 2, maxHealth: 16 }, modelId: 8, hostile: true },
] as const;

export class MobFactory {
  constructor(
    private readonly em: EntityManager,
    private readonly transforms: ComponentStore<typeof TransformDesc>,
    private readonly bodies: ComponentStore<typeof RigidBodyDesc>,
    private readonly healths: ComponentStore<typeof HealthDesc>,
    private readonly aiStates: ComponentStore<typeof AIStateDesc>,
    private readonly mobStats: ComponentStore<typeof MobStatsDesc>,
    private readonly hostileTags: ComponentStore<typeof HostileTagDesc>,
    private readonly friendlyTags: ComponentStore<typeof FriendlyTagDesc>,
    private readonly emitters: ComponentStore<typeof AudioEmitterDesc>,
  ) {}

  spawn(type: MobType, x: number, y: number, z: number): number {
    const entityId = this.em.allocate();
    const entityIndex = EntityManager.indexOf(entityId);
    const cfg = MOB_CONFIGS[type];

    const tRow = this.transforms.add(entityIndex);
    const tPos = this.transforms.data.position;
    const tRot = this.transforms.data.rotation;
    const tScale = this.transforms.data.scale;
    tPos[tRow * 3 + 0] = x;
    tPos[tRow * 3 + 1] = y;
    tPos[tRow * 3 + 2] = z;
    tRot[tRow * 4 + 3] = 1;
    tScale[tRow * 3 + 0] = 1;
    tScale[tRow * 3 + 1] = 1;
    tScale[tRow * 3 + 2] = 1;

    const bRow = this.bodies.add(entityIndex);
    const bVel = this.bodies.data.velocity;
    const bMin = this.bodies.data.aabbMin;
    const bMax = this.bodies.data.aabbMax;
    const halfWidth = cfg.stats.width * 0.5;
    bVel[bRow * 3 + 0] = 0;
    bVel[bRow * 3 + 1] = 0;
    bVel[bRow * 3 + 2] = 0;
    bMin[bRow * 3 + 0] = -halfWidth;
    bMin[bRow * 3 + 1] = 0;
    bMin[bRow * 3 + 2] = -halfWidth;
    bMax[bRow * 3 + 0] = halfWidth;
    bMax[bRow * 3 + 1] = cfg.stats.height;
    bMax[bRow * 3 + 2] = halfWidth;
    this.bodies.data.drag[bRow] = 0.92;
    this.bodies.data.gravity[bRow] = 20;
    this.bodies.data.isFluid[bRow] = 0;
    this.bodies.data.onGround[bRow] = 0;

    const sRow = this.mobStats.add(entityIndex);
    const stats = this.mobStats.data;
    stats.width[sRow] = cfg.stats.width;
    stats.height[sRow] = cfg.stats.height;
    stats.eyeHeight[sRow] = cfg.stats.eyeHeight;
    stats.moveSpeed[sRow] = cfg.stats.moveSpeed;
    stats.attackDamage[sRow] = cfg.stats.attackDamage;
    stats.maxHealth[sRow] = cfg.stats.maxHealth;
    stats.modelId[sRow] = cfg.modelId;

    const hRow = this.healths.add(entityIndex);
    this.healths.data.hp[hRow] = Math.round(cfg.stats.maxHealth);
    this.healths.data.maxHp[hRow] = Math.round(cfg.stats.maxHealth);
    this.healths.data.regenCd[hRow] = 0;

    const aRow = this.aiStates.add(entityIndex);
    this.aiStates.data.state[aRow] = 0;
    this.aiStates.data.attackCd[aRow] = 0;
    this.aiStates.data.pathHead[aRow] = 0;
    this.aiStates.data.pathLen[aRow] = 0;
    this.aiStates.data.targetEntity[aRow] = 0xffff_ffff;

    if (cfg.hostile) {
      this.hostileTags.add(entityIndex);
    } else {
      this.friendlyTags.add(entityIndex);
    }

    const eRow = this.emitters.add(entityIndex);
    this.emitters.data.cooldown[eRow] = 0;
    this.emitters.data.pitch[eRow] = cfg.hostile ? 0.95 : 1.05;
    this.emitters.data.volume[eRow] = cfg.hostile ? 0.9 : 0.65;

    return entityId;
  }
}
