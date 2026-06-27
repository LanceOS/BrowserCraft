#include <catch2/catch_test_macros.hpp>
#include "engine/save/WorldMetadata.hpp"
#include "engine/save/WorldNamingService.hpp"
#include "engine/save/WorldListService.hpp"
#include "engine/save/SettingsRepository.hpp"
#include "engine/save/SaveOrchestrator.hpp"
#include "engine/core/RenderDistanceLimits.hpp"
#include <filesystem>
#include <cstdio>

namespace fs = std::filesystem;

TEST_CASE("WorldMetadata round-trip serialization", "[save][metadata]") {
  auto tmpDir = fs::temp_directory_path() / "terrain_test_meta";
  fs::remove_all(tmpDir);
  fs::create_directories(tmpDir);
  auto metaPath = tmpDir / "world.meta";

  // Create metadata
  auto meta = terrain::WorldMetadata::create("My World", "my_world", 12345, 0);
  REQUIRE(meta.displayName() == "My World");
  REQUIRE(meta.displaySlug() == "my_world");
  REQUIRE(meta.seed == 12345);
  REQUIRE(meta.gameMode == 0);
  REQUIRE(meta.lastPlayedTimestamp > 0); // should be set to current time

  // Write to disk
  REQUIRE(meta.write(metaPath));

  // Read back
  auto loaded = terrain::WorldMetadata::read(metaPath);
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->displayName() == "My World");
  REQUIRE(loaded->displaySlug() == "my_world");
  REQUIRE(loaded->seed == 12345);
  REQUIRE(loaded->gameMode == 0);
  REQUIRE(loaded->lastPlayedTimestamp == meta.lastPlayedTimestamp);

  // Verify magic/version
  REQUIRE(loaded->magic == terrain::WorldMetadata::MAGIC);
  REQUIRE(loaded->version == terrain::WorldMetadata::CURRENT_VERSION);

  // Touch timestamp
  loaded->touch();
  REQUIRE(loaded->lastPlayedTimestamp >= meta.lastPlayedTimestamp);

  // Cleanup
  fs::remove_all(tmpDir);
}

TEST_CASE("WorldMetadata invalid file returns nullopt", "[save][metadata]") {
  auto tmpDir = fs::temp_directory_path() / "terrain_test_meta_bad";
  fs::remove_all(tmpDir);
  fs::create_directories(tmpDir);
  auto metaPath = tmpDir / "nonexistent.meta";

  auto result = terrain::WorldMetadata::read(metaPath);
  REQUIRE_FALSE(result.has_value());

  fs::remove_all(tmpDir);
}

TEST_CASE("WorldNamingService slug generation", "[save][naming]") {
  SECTION("simple name") {
    REQUIRE(terrain::WorldNamingService::generateSlug("My World") == "my_world");
  }
  SECTION("spaces and special chars") {
    REQUIRE(terrain::WorldNamingService::generateSlug("Hello!!! World?") == "hello_world");
  }
  SECTION("already safe") {
    REQUIRE(terrain::WorldNamingService::generateSlug("test_world_1") == "test_world_1");
  }
  SECTION("leading/trailing spaces") {
    REQUIRE(terrain::WorldNamingService::generateSlug("  spaces  ") == "spaces");
  }
  SECTION("only special chars") {
    REQUIRE(terrain::WorldNamingService::generateSlug("!!! ???") == "world");
  }
  SECTION("mixed case") {
    REQUIRE(terrain::WorldNamingService::generateSlug("MyCoolWorld") == "mycoolworld");
  }
  SECTION("numbers") {
    REQUIRE(terrain::WorldNamingService::generateSlug("World 123") == "world_123");
  }
}

TEST_CASE("WorldNamingService name validation", "[save][naming]") {
  SECTION("empty name") {
    auto err = terrain::WorldNamingService::validateName("");
    REQUIRE_FALSE(err.empty());
  }
  SECTION("too long") {
    std::string longName(61, 'a');
    auto err = terrain::WorldNamingService::validateName(longName);
    REQUIRE_FALSE(err.empty());
  }
  SECTION("no alphanumeric") {
    auto err = terrain::WorldNamingService::validateName("___");
    REQUIRE_FALSE(err.empty());
  }
  SECTION("valid name") {
    auto err = terrain::WorldNamingService::validateName("My World");
    REQUIRE(err.empty());
  }
  SECTION("valid short name") {
    auto err = terrain::WorldNamingService::validateName("a");
    REQUIRE(err.empty());
  }
}

TEST_CASE("WorldNamingService collision detection", "[save][naming]") {
  auto tmpDir = fs::temp_directory_path() / "terrain_test_naming";
  fs::remove_all(tmpDir);
  fs::create_directories(tmpDir);

  // Create a fake world directory with metadata
  auto worldDir = tmpDir / "existing_world";
  fs::create_directories(worldDir);
  auto meta = terrain::WorldMetadata::create("Existing World", "existing_world", 0, 0);
  REQUIRE(meta.write(worldDir / "world.meta"));

  terrain::WorldNamingService naming(tmpDir.string());

  SECTION("detects taken name") {
    REQUIRE(naming.isNameTaken("Existing World"));
  }
  SECTION("detects free name") {
    REQUIRE_FALSE(naming.isNameTaken("New World"));
  }
  SECTION("detects taken slug") {
    REQUIRE(naming.isSlugTaken("existing_world"));
  }
  SECTION("detects free slug") {
    REQUIRE_FALSE(naming.isSlugTaken("free_slug"));
  }

  SECTION("next available slug - base not taken") {
    auto slug = naming.nextAvailableSlug("Free Name");
    REQUIRE(slug == "free_name");
  }
  SECTION("next available slug - base taken") {
    auto slug = naming.nextAvailableSlug("Existing World");
    // The base 'existing_world' is taken, so we get a numbered alternative
    REQUIRE(slug != "existing_world");
    REQUIRE(slug.find("existing_world") == 0);
  }

  fs::remove_all(tmpDir);
}

TEST_CASE("WorldListService scans and sorts worlds", "[save][worldlist]") {
  auto tmpDir = fs::temp_directory_path() / "terrain_test_list";
  fs::remove_all(tmpDir);

  // Create multiple world directories with metadata
  auto createWorld = [&](const std::string& name, const std::string& slug,
                          uint32_t seed, int64_t timestamp) {
    auto dir = tmpDir / slug;
    fs::create_directories(dir);
    auto meta = terrain::WorldMetadata::create(name, slug, seed, 0);
    meta.lastPlayedTimestamp = timestamp;
    REQUIRE(meta.write(dir / "world.meta"));
  };

  // Create worlds with different timestamps
  createWorld("Alpha", "alpha", 100, 1000);
  createWorld("Beta", "beta", 200, 3000);
  createWorld("Gamma", "gamma", 300, 2000);

  terrain::WorldListService listService(tmpDir.string());

  SECTION("finds all worlds") {
    REQUIRE(listService.count() == 3);
  }

  SECTION("sorts by timestamp descending") {
    REQUIRE(listService.count() == 3);
    // Beta (3000) should be first, Gamma (2000) second, Alpha (1000) third
    REQUIRE(listService.get(0) != nullptr);
    REQUIRE(listService.get(0)->displayName() == "Beta");
    REQUIRE(listService.get(1)->displayName() == "Gamma");
    REQUIRE(listService.get(2)->displayName() == "Alpha");
  }

  SECTION("find by slug") {
    auto* found = listService.findBySlug("alpha");
    REQUIRE(found != nullptr);
    REQUIRE(found->displayName() == "Alpha");
    REQUIRE(found->seed == 100);
  }

  SECTION("find by slug not found") {
    REQUIRE(listService.findBySlug("nonexistent") == nullptr);
  }

  SECTION("empty directory") {
    auto emptyDir = fs::temp_directory_path() / "terrain_test_list_empty";
    fs::remove_all(emptyDir);
    fs::create_directories(emptyDir);
    terrain::WorldListService emptyService(emptyDir.string());
    REQUIRE(emptyService.empty());
    REQUIRE(emptyService.count() == 0);
    fs::remove_all(emptyDir);
  }

  SECTION("refresh rescans") {
    // Add a new world after initial scan
    createWorld("Delta", "delta", 400, 4000);
    listService.refresh();
    REQUIRE(listService.count() == 4);
    // Delta (4000) should now be first
    REQUIRE(listService.get(0)->displayName() == "Delta");
  }

  fs::remove_all(tmpDir);
}

TEST_CASE("SettingsRepository CRUD operations", "[save][settings]") {
  auto tmpDir = fs::temp_directory_path() / "terrain_test_settings";
  fs::remove_all(tmpDir);
  fs::create_directories(tmpDir);
  auto dbPath = tmpDir / "settings.db";

  terrain::SettingsRepository repo(dbPath.string());

  SECTION("default values") {
    REQUIRE(repo.get("nonexistent") == "");
    REQUIRE(repo.get("nonexistent", "default") == "default");
    REQUIRE(repo.getInt("nonexistent", 42) == 42);
    REQUIRE(repo.getFloat("nonexistent", 3.14f) == 3.14f);
    REQUIRE(repo.getBool("nonexistent", true) == true);
  }

  SECTION("string set and get") {
    repo.set("name", "test_value");
    REQUIRE(repo.get("name") == "test_value");
  }

  SECTION("int set and get") {
    repo.setInt("render_distance", 8);
    REQUIRE(repo.getInt("render_distance") == 8);
  }

  SECTION("float set and get") {
    repo.setFloat("volume", 0.75f);
    REQUIRE(repo.getFloat("volume") == 0.75f);
  }

  SECTION("bool set and get") {
    repo.setBool("show_fps", true);
    REQUIRE(repo.getBool("show_fps") == true);
    repo.setBool("show_fps", false);
    REQUIRE(repo.getBool("show_fps") == false);
  }

  SECTION("has key") {
    repo.set("exists", "yes");
    REQUIRE(repo.has("exists"));
    REQUIRE_FALSE(repo.has("missing"));
  }

  SECTION("remove key") {
    repo.set("temp", "value");
    REQUIRE(repo.has("temp"));
    repo.remove("temp");
    REQUIRE_FALSE(repo.has("temp"));
  }

  SECTION("overwrite value") {
    repo.set("key", "first");
    repo.set("key", "second");
    REQUIRE(repo.get("key") == "second");
  }

  SECTION("clear all") {
    repo.set("a", "1");
    repo.set("b", "2");
    repo.clear();
    REQUIRE_FALSE(repo.has("a"));
    REQUIRE_FALSE(repo.has("b"));
  }

  fs::remove_all(tmpDir);
}

TEST_CASE("SettingsRepository persists across sessions", "[save][settings][persist]") {
  auto tmpDir = fs::temp_directory_path() / "terrain_test_settings_persist";
  fs::remove_all(tmpDir);
  fs::create_directories(tmpDir);
  auto dbPath = tmpDir / "settings.db";

  // First session: write data
  {
    terrain::SettingsRepository repo(dbPath.string());
    repo.set("name", "test_value");
    repo.setInt("render_distance", 8);
    repo.setFloat("volume", 0.75f);
    repo.setBool("show_fps", false);
    // Destructor flushes and closes
  }

  // Second session: verify data persists
  {
    terrain::SettingsRepository repo(dbPath.string());
    REQUIRE(repo.get("name") == "test_value");
    REQUIRE(repo.getInt("render_distance") == 8);
    REQUIRE(repo.getFloat("volume") == 0.75f);
    REQUIRE(repo.getBool("show_fps") == false);
  }

  fs::remove_all(tmpDir);
}

TEST_CASE("SaveOrchestrator clamps loaded render distance", "[save][orchestrator][settings]") {
  auto tmpDir = fs::temp_directory_path() / "terrain_test_orch_settings";
  fs::remove_all(tmpDir);
  fs::create_directories(tmpDir);

  {
    terrain::SettingsRepository repo((tmpDir / "settings.db").string());
    repo.setInt("renderDistance", 40);
    repo.setBool("showFps", false);
  }

  terrain::SaveOrchestrator orch(tmpDir.string());
  auto settings = orch.loadSettings();
  REQUIRE(settings.renderDistance == terrain::MAX_RENDER_DISTANCE);
  REQUIRE(settings.showFps == false);

  fs::remove_all(tmpDir);
}

TEST_CASE("SaveOrchestrator buildWorldEntries", "[save][orchestrator]") {
  std::vector<terrain::WorldMetadata> worlds;

  // Empty list
  auto entries = terrain::SaveOrchestrator::buildWorldEntries(worlds);
  REQUIRE(entries.empty());

  // Single world
  auto meta1 = terrain::WorldMetadata::create("Alpha", "alpha", 100, 0);
  meta1.lastPlayedTimestamp = 1000;
  worlds.push_back(meta1);

  entries = terrain::SaveOrchestrator::buildWorldEntries(worlds);
  REQUIRE(entries.size() == 1);
  REQUIRE(entries[0].name == "Alpha");
  REQUIRE(entries[0].slug == "alpha");
  REQUIRE(entries[0].seed == 100);
  REQUIRE(entries[0].gameMode == 0);
  REQUIRE(entries[0].lastPlayedTimestamp == 1000);

  // Multiple worlds
  auto meta2 = terrain::WorldMetadata::create("Beta", "beta", 200, 1);
  meta2.lastPlayedTimestamp = 2000;
  worlds.push_back(meta2);

  entries = terrain::SaveOrchestrator::buildWorldEntries(worlds);
  REQUIRE(entries.size() == 2);
  REQUIRE(entries[0].name == "Alpha");
  REQUIRE(entries[1].name == "Beta");
  REQUIRE(entries[1].gameMode == 1);
}

TEST_CASE("SaveOrchestrator new world preparation", "[save][orchestrator]") {
  auto tmpDir = fs::temp_directory_path() / "terrain_test_orch_new";
  fs::remove_all(tmpDir);
  fs::create_directories(tmpDir);

  terrain::SaveOrchestrator orch(tmpDir.string());

  SECTION("creates valid new world") {
    uint32_t seed = 0;
    auto result = orch.prepareNewWorld("My New World", terrain::GameMode::Survival, seed);
    REQUIRE(result.error.empty());
    REQUIRE_FALSE(result.slug.empty());
    REQUIRE(result.slug == "my_new_world");
    REQUIRE(seed != 0);
  }

  SECTION("rejects empty name") {
    uint32_t seed = 0;
    auto result = orch.prepareNewWorld("", terrain::GameMode::Survival, seed);
    REQUIRE_FALSE(result.error.empty());
  }

  SECTION("rejects duplicate name") {
    // First world: succeeds
    uint32_t seed1 = 0;
    auto result1 = orch.prepareNewWorld("My World", terrain::GameMode::Creative, seed1);
    REQUIRE(result1.error.empty());
    REQUIRE(result1.slug == "my_world");

    // Create the world directory so the name is taken
    fs::create_directories(tmpDir / "my_world");
    auto meta = terrain::WorldMetadata::create("My World", "my_world", seed1, 1);
    REQUIRE(meta.write(tmpDir / "my_world" / "world.meta"));
    orch.refreshWorldList();

    // Second attempt with same name: fails
    uint32_t seed2 = 0;
    auto result2 = orch.prepareNewWorld("My World", terrain::GameMode::Survival, seed2);
    REQUIRE_FALSE(result2.error.empty());
  }

  SECTION("generates unique slugs for duplicate slugs") {
    uint32_t seed = 0;
    auto result1 = orch.prepareNewWorld("My World", terrain::GameMode::Survival, seed);
    REQUIRE(result1.slug == "my_world");

    // Create the directory to make slug taken
    fs::create_directories(tmpDir / "my_world");
    auto meta1 = terrain::WorldMetadata::create("My World", "my_world", seed, 0);
    REQUIRE(meta1.write(tmpDir / "my_world" / "world.meta"));
    orch.refreshWorldList();

    // Second world with different name that maps to same slug
    uint32_t seed2 = 0;
    auto result2 = orch.prepareNewWorld("my_world", terrain::GameMode::Survival, seed2);
    REQUIRE(result2.error.empty());
    // Should get a numbered suffix
    REQUIRE(result2.slug.find("my_world") == 0);
    REQUIRE(result2.slug != "my_world");
  }

  fs::remove_all(tmpDir);
}

TEST_CASE("SaveOrchestrator load world preparation", "[save][orchestrator]") {
  auto tmpDir = fs::temp_directory_path() / "terrain_test_orch_load";
  fs::remove_all(tmpDir);
  fs::create_directories(tmpDir);

  // Create a world directory with metadata
  fs::create_directories(tmpDir / "existing_world");
  auto meta = terrain::WorldMetadata::create("Existing World", "existing_world", 42, 0);
  REQUIRE(meta.write(tmpDir / "existing_world" / "world.meta"));

  terrain::SaveOrchestrator orch(tmpDir.string());
  orch.refreshWorldList();

  SECTION("load by slug") {
    auto result = orch.prepareLoadWorld("existing_world");
    REQUIRE(result.error.empty());
    REQUIRE(result.slug == "existing_world");
  }

  SECTION("load by display name") {
    auto result = orch.prepareLoadWorld("Existing World");
    REQUIRE(result.error.empty());
    REQUIRE(result.slug == "existing_world");
  }

  SECTION("rejects nonexistent world") {
    auto result = orch.prepareLoadWorld("nonexistent");
    REQUIRE_FALSE(result.error.empty());
  }

  SECTION("rejects empty identifier") {
    auto result = orch.prepareLoadWorld("");
    REQUIRE_FALSE(result.error.empty());
  }

  fs::remove_all(tmpDir);
}
