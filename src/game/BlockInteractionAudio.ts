import { AudioNodePool } from "../engine/audio/AudioNodePool.js";
import { AudioRegistry, SoundId } from "../content/audio/AudioRegistry.js";
import { BlockRegistry } from "../world/BlockRegistry.js";

export class BlockInteractionAudio {
  constructor(
    private readonly pool: AudioNodePool,
    private readonly registry: AudioRegistry,
    private readonly blocks: BlockRegistry,
  ) {}

  onBlockBroken(x: number, y: number, z: number, blockId: number): void {
    if (blockId === 0) return;
    const def = this.blocks.tryGet(blockId);
    if (!def) return;

    let soundId = SoundId.STONE_BREAK;
    if (blockId === 2 || blockId === 3 || def.material.foliage) {
      soundId = SoundId.GRASS_BREAK;
    } else if (blockId === 5 || blockId === 17 || blockId === 54 || blockId === 58) {
      soundId = SoundId.WOOD_BREAK;
    }

    const buffer = this.registry.get(soundId);
    if (!buffer) return;
    this.pool.playOneShot(buffer, x + 0.5, y + 0.5, z + 0.5, 1, 0.85 + Math.random() * 0.3);
  }
}
