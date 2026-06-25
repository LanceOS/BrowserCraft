## Architectural Analysis & Implementation Plan

### Design Patterns to Apply

* **Repository Pattern** -- Abstract chunk storage behind an IWorldRepository interface to decouple world generation from persistence.
* **Strategy Pattern** -- Different serialization strategies (binary, JSON, compressed) for chunk data.
* **Unit of Work** -- Group chunk saves and metadata updates into atomic transactions.
* **Observer / Event Pattern** -- Emit events (WorldSaved, WorldLoaded, SaveFailed) so UI and autosave react without tight coupling.

### Code Decoupling Points

* World Generation vs. Persistence: Generator calls repository.save(chunk) without knowing the backend.
* World List UI vs. Storage: UI queries a WorldMetadataService, not the filesystem directly.
* Settings Saving: Separate SQLite-backed SettingsRepository decoupled from world save logic.
* Name Collision Logic: Centralize in a WorldNamingService rather than scattering across UI and generation.

### Suggested Module Breakdown

```javascript
/Assets/Scripts/SaveSystem/
  SaveManager.cs              -- Orchestrator
  /Repositories/
    IWorldRepository.cs       -- Interface
    FileSystemWorldRepository.cs
    SettingsRepository.cs     -- SQLite for user settings
  /Models/
    WorldMetadata.cs          -- Name, seed, last-played timestamp, slug
    ChunkData.cs              -- Serializable chunk structure
  /Services/
    WorldNamingService.cs     -- Uniqueness validation, slug generation
    WorldListService.cs       -- Query available worlds, sort by recency
```

### Recommended Implementation Order


1. Define IWorldRepository interface and WorldMetadata model -- unblocks all other work.
2. Implement world list UI querying WorldListService so users can see their worlds.
3. Add name-collision enforcement in WorldNamingService.
4. Build the settings SQLite module as a separate parallel task.
5. Layer on atomic chunk persistence last (highest data-integrity risk).

### Key Risks

* Partial world corruption on crash -- use write-ahead temp files + rename-on-complete.
* Autosave vs. manual save distinction -- maintain a save type flag.
* SQLite threading concurrency -- use WAL mode journaling.
* World list performance -- cache metadata in an index rather than iterating folders.


