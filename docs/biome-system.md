# Biome System

## Overview

The biome system determines terrain surface composition (blocks), height modulation, and — in future phases — vegetation, mob spawning, weather, and events based on procedurally generated climate data. It is designed around three architectural layers:

```
Climate Noise  ──→  ClimateSample  ──→  Biome Class  ──→  Terrain
(BiomeSampler)      {temp, humidity}    (polymorphic)      (WorldGenPipeline)
                           │                    ▲
                           ▼                    │
                     BiomeFactory ──────────────┘
                     (routing, blending)
```

**Key design principles:**
1. **Separation of concerns** — Noise generation (BiomeSampler) is separate from classification logic (BiomeFactory) which is separate from biome data (concrete Biome classes).
2. **Open/Closed** — Adding a new biome requires only a new class + factory registration. No existing code is modified.
3. **Pure functions where possible** — BiomeFactory methods are static and operate on ClimateSample values with no mutable state.
4. **Smooth transitions** — Biomes blend via Hermite smoothstep weighting rather than hard edges.

---

## Architecture

### 1. Core Data Types (`BiomeData.hpp`)

| Type | Purpose |
|------|---------|
| `ClimateSample` | Bundles `temperature` + `humidity` in [0, 1] to avoid duplicate noise evaluations |
| `BiomeWeights` | 6 floats (plains, desert, forest, mountains, swamp, ocean) summing to ~1 after normalisation |
| `BiomeId` | `uint8_t` enum — Plains=0, Desert=1, Forest=2, Mountains=3, Swamp=4, Ocean=5 |
| `ALL_BIOME_IDS` | `constexpr std::array` of all 6 `BiomeId` values for iteration |

#### Climate Threshold Constants

All biome boundary thresholds are defined as `inline constexpr` floats in `BiomeData.hpp`. They serve as the **single source of truth** used by both `BiomeFactory::pick()` (hard cutoff) and `BiomeFactory::computeWeights()` (smooth transition):

| Constant | Value | Used By |
|----------|-------|---------|
| `kClimateScale` | 0.008 | Noise sampling frequency |
| `kMountainTempThreshold` | 0.28 | Mountains (cold) |
| `kMountainTransWidth` | 0.08 | Mountain transition width |
| `kDesertTempThreshold` | 0.65 | Desert (hot) |
| `kDesertHumidThreshold` | 0.35 | Desert (dry) |
| `kDesertTransWidth` | 0.08 | Desert transition width |
| `kSwampHumidThreshold` | 0.72 | Swamp (wet) |
| `kSwampHumidTransWidth` | 0.10 | Swamp humidity transition |
| `kSwampTempThreshold` | 0.35 | Swamp (warm) |
| `kSwampTempTransWidth` | 0.08 | Swamp temperature transition |
| `kForestHumidThreshold` | 0.55 | Forest (humid) |
| `kForestTransWidth` | 0.08 | Forest transition width |
| `kOceanTempThreshold` | 0.42 | Ocean (cool) |
| `kOceanHumidThreshold` | 0.40 | Ocean (dry) |
| `kOceanTransWidth` | 0.08 | Ocean transition width |

#### `smoothEdge()` Function

```cpp
inline constexpr float smoothEdge(float value, float threshold, float width);
```

Hermite smoothstep: maps `[threshold - width/2, threshold + width/2]` → `[0, 1]` with smooth ease-in-out. Values outside the range clamp to 0 or 1. Defined as a `constexpr` free function so it can be used anywhere without including `<algorithm>`.

```
  1.0 ┤
      │          ┌─────────────────────
      │         ╱
      │        ╱
      │       ╱
      │      ╱
      │     ╱
  0.0 ┤────
      └───────────────────────────
         threshold - w/2    threshold + w/2
```

---

### 2. Climate Noise (`BiomeSampler`)

**File:** `src/content/biomes/BiomeSampler.hpp`, `BiomeSampler.cpp`

`BiomeSampler` owns two `SimplexNoise` instances (temperature and humidity) and provides noise sampling at world coordinates. It implements `IClimateSource` so it can be passed to `WorldGenPipeline` or replaced with a different noise strategy.

```
BiomeSampler
├── m_tempNoise (seed ^ 0xa10beu)
├── m_humidNoise (seed ^ 0xb1d07u)
│
├── sampleTemperature(x, z) → float [0, 1]
├── sampleHumidity(x, z)    → float [0, 1]
└── sampleClimate(x, z)     → ClimateSample  // calls both, bundles result
```

**Noise evaluation:** Simplex noise raw values in [-1, 1] are mapped to [0, 1]:
```cpp
return (raw + 1.0f) * 0.5f;
```

**Climate scale:** `kClimateScale = 0.008f`. At this frequency, the noise changes by ~0.128 units across a 16-block chunk, creating biomes that are roughly 100-200 blocks across — large enough to be visually coherent but varied enough to explore.

#### IClimateSource Interface

**File:** `src/content/biomes/IClimateSource.hpp`

```cpp
class IClimateSource {
public:
  virtual ~IClimateSource() = default;
  virtual ClimateSample sampleClimate(float worldX, float worldZ) const = 0;
};
```

Allows `WorldGenPipeline` to accept any climate noise strategy. Default: `BiomeSampler`. Alternatives could include: flat preset, debug patterns, alternative noise algorithms.

---

### 3. Biome Class Hierarchy (`Biome.hpp`)

**File:** `src/content/biomes/Biome.hpp`

#### Abstract Base

```cpp
class Biome {
public:
  virtual ~Biome() = default;
  virtual BiomeId id() const noexcept = 0;
  virtual uint8_t topBlock() const noexcept = 0;
  virtual uint8_t fillerBlock() const noexcept = 0;
  virtual int32_t surfaceDepth() const noexcept = 0;
  virtual float heightBias() const noexcept = 0;
};
```

#### Concrete Singletons

Each biome is a stateless `final` class with a private constructor and a static `instance()` method:

| Biome Class | ID | Top Block | Filler Block | Depth | Height Bias |
|-------------|----|-----------|--------------|-------|-------------|
| `PlainsBiome` | Plains | GRASS | DIRT | 4 | 0.0 |
| `DesertBiome` | Desert | SAND | SAND | 3 | -3.0 |
| `ForestBiome` | Forest | GRASS | DIRT | 5 | 3.0 |
| `MountainsBiome` | Mountains | GRASS | STONE | 3 | 22.0 |
| `SwampBiome` | Swamp | STONE | STONE | 5 | -3.0 |
| `OceanBiome` | Ocean | SAND | SAND | 3 | -14.0 |

**Pattern:**
```cpp
class PlainsBiome final : public Biome {
public:
  static const PlainsBiome& instance() {
    static PlainsBiome s;
    return s;
  }
  BiomeId id() const noexcept override { return BiomeId::Plains; }
  uint8_t topBlock() const noexcept override { return MaterialId::GRASS; }
  // ...
private:
  PlainsBiome() = default;
};
```

**Adding a new biome:**
1. Add value to `BiomeId` enum in `BiomeData.hpp`
2. Add singleton class in `Biome.hpp`
3. Register in `BiomeFactory::forId()` and `BiomeFactory::pick()` and `BiomeFactory::computeWeights()` in `BiomeFactory.cpp`
4. No other code changes needed (Open/Closed principle)

---

### 4. Biome Factory (`BiomeFactory`)

**File:** `src/content/biomes/BiomeFactory.hpp`, `BiomeFactory.cpp`

Stateless routing layer. All methods are `static`.

#### `forId(BiomeId) → const Biome&`

O(1) lookup via switch statement. Returns the singleton instance for the given ID. Used for debug rendering, save/load, and runtime queries.

#### `pick(float temperature, float humidity) → const Biome&`

Hard-cutoff classification. Priority order (first match wins):

```
Desert:  t > 0.65 && h < 0.35   (hot + dry)
Swamp:   h > 0.72 && t > 0.35   (wet + warm)
Mountains: t < 0.28              (cold)
Forest:  h > 0.55                (humid)
Ocean:   t < 0.42 && h < 0.40   (cool + dry)
Default: Plains                  (everything else)
```

This priority order ensures:
- Deserts form in hot, dry regions only
- Swamps require warmth AND high humidity
- Mountains are the coldest regions
- Forests occupy humid regions that aren't too cold
- Oceans fill cool, dry areas between mountains and plains
- Plains are the fallback for moderate climates

#### `mountainWeight(ClimateSample) → float`

Smooth [0, 1] value derived solely from temperature. Uses `smoothEdge()` centered at `kMountainTempThreshold` (0.28) with width `kMountainTransWidth` (0.08). Used by `WorldGenPipeline` to modulate mountain amplification noise.

```
mountainWeight
  1.0 ┤
      │    ╲
      │     ╲
      │      ╲
      │       ╲
      │        ╲
  0.0 ┤─────────╲────────────
      └───────────────────────
        0.20  0.28  0.36  t
```

#### `computeWeights(ClimateSample) → BiomeWeights`

Computes per-biome weights with smooth transitions. The algorithm:

1. **Mountains**: `1 - smoothEdge(t, 0.28, 0.08)` — high when cold
2. **Desert**: `smoothEdge(t, 0.65, 0.08) * (1 - smoothEdge(h, 0.35, 0.08))` — hot AND dry
3. **Swamp**: `smoothEdge(h, 0.72, 0.10) * smoothEdge(t, 0.35, 0.08)` — wet AND warm
4. **Forest**: `smoothEdge(h, 0.55, 0.08) * (1 - mountains) * (1 - swamp)` — humid, suppressed where mountains/swamp dominate
5. **Ocean**: `(1 - smoothEdge(t, 0.42, 0.08)) * (1 - smoothEdge(h, 0.40, 0.08)) * (1 - mountains)` — cool & dry, suppressed by mountains
6. **Plains**: remainder (`max(0, 1 - sum of others)`)
7. **Normalise**: divide each weight by the total, ensuring they sum to 1

The suppression terms (`1 - mountains`, `1 - swamp`) mirror the priority ordering in `pick()`, ensuring smooth blending doesn't produce unexpected biome combinations.

#### `blendedHeightBias(ClimateSample) → float`

Calls `computeWeights()` and returns the weighted sum of all biome height biases:
```cpp
return w.mountains * 22 + w.desert * (-3) + w.swamp * (-3) + w.forest * 3
     + w.ocean * (-14) + w.plains * 0;
```

This produces a smoothly varying height field without hard walls at biome boundaries.

---

### 5. Biome Surface Rules vs. Height Bias

The biome system affects terrain in two ways:

**Surface blocks** are determined by the **dominant biome** at each column (via `pick()`) — only one biome's top/filler blocks are used. This prevents visual blending artifacts (e.g., half-grass, half-sand blocks).

**Terrain height** is determined by the **blended height bias** (via `blendedHeightBias()`) — all 6 biomes contribute through their normalized weights. This prevents flat walls where biomes transition.

This split design means:
- At a mountain/plains boundary, the height smoothly transitions (no wall) while the surface blocks switch abruptly at the `pick()` threshold (dominant biome changes).
- This is the best compromise: smooth terrain with clean surface block boundaries.

---

### 6. Integration with World Generation

**File:** `src/world/generation/WorldGenPipeline.cpp`

In the chunk generation loop (per column):

```cpp
// 1. Sample climate once (2 noise calls instead of 5)
auto climate = m_climateSource->sampleClimate(worldX, worldZ);

// 2. Get dominant biome for surface rules
const auto& activeBiome = biome::BiomeFactory::pick(climate);

// 3. Compute blended height bias (smooth transition)
float heightBias = biome::BiomeFactory::blendedHeightBias(climate);

// 4. Mountain amplification (smooth weight)
float mountainWeight = biome::BiomeFactory::mountainWeight(climate);

// 5. Determine surface height
int32_t surfaceY = static_cast<int32_t>(
    cfg.baseHeight
    + continental * cfg.continentalAmplitude
    + detail * cfg.detailAmplitude
    + heightBias
    + mountainExtra);
surfaceY = std::clamp(surfaceY, 1, terrainMaxY);

// 6. If below sea level and not desert/ocean, fill with water
const bool noWater = (activeBiome.id() == biome::BiomeId::Desert
                   || activeBiome.id() == biome::BiomeId::Ocean);

// 7. Apply surface layering
if (y == surfaceY) {
    density data[index] = activeBiome.topBlock();
} else if (y > surfaceY - activeBiome.surfaceDepth()) {
    density data[index] = activeBiome.fillerBlock();
} else {
    density data[index] = MaterialId::STONE;
}
```

---

### 7. Terrain Height Composition

The final terrain height at each column is:

```
surfaceY = baseHeight (64)
         + continental_noise × 40    (large-scale landforms, 3 octaves)
         + detail_noise × 14         (hills/valleys, 2 octaves)
         + blendedHeightBias          (biome-specific, -14 to +22)
         + mountainExtra              (mountain amplification, 0 to ~28)
```

Where:
- **continental_noise**: fractal noise from `m_continentalNoise` with 3 octaves (lacunarity 2.0, persistence 0.5) at scale 0.008
- **detail_noise**: fractal noise from `m_detailNoise` with 2 octaves (lacunarity 2.0, persistence 0.5) at scale 0.05
- **blendedHeightBias**: weighted sum of all 6 biome height biases (see §4)
- **mountainExtra**: `mountainWeight × fractalNoise(...) × 28`, always upward-shaping (negative noise values are inverted and damped by 0.6)

The mountain amplification noise only activates where `mountainWeight` is high (cold climates), creating steep peaks without affecting flat terrain.

---

### 8. Noise Instances

`WorldGenPipeline` owns 3 `SimplexNoise` instances, each with a unique seed offset for independence:

| Instance | Seed Offset | Used For |
|----------|-------------|----------|
| `m_continentalNoise` | `seed ^ 0x1a2b3cu` | Continental height, mountain amplification |
| `m_detailNoise` | `seed ^ 0x4d5e6fu` | Regional/detail height |
| `m_densityNoise` | `seed` | 3D underground cavity carving |

`BiomeSampler` owns 2 additional instances:

| Instance | Seed Offset | Used For |
|----------|-------------|----------|
| `m_tempNoise` | `seed ^ 0xa10beu` | Temperature field |
| `m_humidNoise` | `seed ^ 0xb1d07u` | Humidity field |

---

### 9. Chunk Validation & Retry

**File:** `src/world/World.cpp`, `src/world/Chunk.hpp`

After generation completes, `World::onWorldGenDone()` validates the chunk by checking for bedrock at the 4 corner columns. If bedrock is absent and the chunk hasn't exceeded `MAX_CHUNK_GEN_RETRIES` (3), it's re-queued for generation.

```cpp
const int32_t checkIndices[] = {0, sz - 1, (sz - 1) * sx, sz * sx - 1};
bool hasBedrock = false;
for (int32_t ci : checkIndices) {
    if (slot.density data[ci] != 0) { hasBedrock = true; break; }
}
if (!hasBedrock && chunk->genRetries < MAX_CHUNK_GEN_RETRIES) {
    ++chunk->genRetries;
    chunk->state = ChunkState::QueuedGen;
    m_jobQueue.pushGen(chunk->slotIndex, chunk->chunkX, chunk->chunkZ);
    return;
}
```

---

### 10. Extending the System

#### Adding a New Biome

1. **`BiomeData.hpp`**: Add value to `BiomeId` enum, add threshold constants
2. **`Biome.hpp`**: Add singleton class inheriting from `Biome`
3. **`BiomeFactory.cpp`**: Register in `forId()`, `pick()`, `computeWeights()`
4. **`BiomeData.hpp`**: Add entry to `ALL_BIOME_IDS` array
5. **Tests**: Add pick test, forId test, update ALL_BIOME_IDS test

The `BiomeWeights` struct will need the new biome's weight field added, and `computeWeights()` will compute it.

#### Adding a Custom Climate Strategy

1. Implement `IClimateSource` interface
2. Pass to `WorldGenPipeline(IClimateSource&, seed, config)` constructor

#### Adding Seasonal/Elevation Variation

Override `topBlock()`, `fillerBlock()`, or `surfaceDepth()` in a concrete biome class to return different values based on world parameters, game time, or elevation. The virtual methods accept these parameters via future signature extensions (e.g., `topBlock(float worldY, int season)`).

---

### 11. File Index

| File | Purpose |
|------|---------|
| `src/content/biomes/BiomeData.hpp` | Core types: ClimateSample, BiomeWeights, BiomeId, threshold constants, smoothEdge(), ALL_BIOME_IDS |
| `src/content/biomes/Biome.hpp` | Abstract Biome base class + 6 concrete singleton classes |
| `src/content/biomes/BiomeFactory.hpp` | Static factory: forId, pick, mountainWeight, computeWeights, blendedHeightBias |
| `src/content/biomes/BiomeFactory.cpp` | Factory implementation |
| `src/content/biomes/IClimateSource.hpp` | Interface for climate noise strategies |
| `src/content/biomes/BiomeSampler.hpp` | Climate noise (temp + humidity SimplexNoise), implements IClimateSource |
| `src/content/biomes/BiomeSampler.cpp` | Noise sampling + convenience wrappers |
| `src/world/generation/WorldGenPipeline.hpp` | Terrain pipeline, owns noise instances + climate source pointer |
| `src/world/generation/WorldGenPipeline.cpp` | Chunk generation loop, surface layering, cave/ore distribution |
| `tests/test_biome_sampler.cpp` | 105 test cases covering pick, computeWeights, blendedHeightBias, forId, ALL_BIOME_IDS |

---

### 12. Test Coverage

105 test cases (1250 assertions) in `test_biome_sampler.cpp`:

| Test | What It Verifies |
|------|------------------|
| `BiomeFactory pick returns valid biome` | All 6 biomes reachable via pick() |
| `BiomeSampler sampleBiome returns valid biome` | Runtime noise sampling produces valid result |
| `BiomeSampler blendedHeightBias is smooth` | Finite, in-range, different at distant coords |
| `BiomeFactory blendedHeightBias with known climate` | Pure mountain/desert/plains values match expectations |
| `BiomeFactory mountainWeight with known temperature` | Full/zero/intermediate weights at thresholds |
| `BiomeSampler blended bias stays within biome extremes` | All positions bounded by DesertBiome (-3) and MountainsBiome (22) height biases |
| `BiomeFactory computeWeights sums to ~1` | 8 climate points, all weights in [0,1], sum ≈ 1 |
| `BiomeFactory forId covers all biomes` | ALL_BIOME_IDS has 6 entries, forId returns correct values |
