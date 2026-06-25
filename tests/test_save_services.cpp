#include <catch2/catch_test_macros.hpp>
#include "engine/save/WorldMetadata.hpp"
#include "engine/save/WorldNamingService.hpp"
#include "engine/save/WorldListService.hpp"
#include "engine/save/SettingsRepository.hpp"
#include <filesystem>
#include <cstdio>

namespace fs = std::filesystem;

TEST_CASE("WorldMetadata round-trip serialization", "[save][metadata]") {
  auto tmpDir = fs::temp_directory_path() / "voxel_test_meta";
  fs::remove_all(tmpDir);
  fs::create_directories(tmpDir);
  auto metaPath = tmpDir / "world.meta";

  // Create metadata
  auto meta = voxel::WorldMetadata::create("My World", "my_world", 12345, 0);
  REQUIRE(meta.displayName() == "My World");
  REQUIRE(meta.displaySlug() == "my_world");
  REQUIRE(meta.seed == 12345);
  REQUIRE(meta.gameMode == 0);
  REQUIRE(meta.lastPlayedTimestamp > 0); // should be set to current time

  // Write to disk
  REQUIRE(meta.write(metaPath));

  // Read back
  auto loaded = voxel::WorldMetadata::read(metaPath);
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->displayName() == "My World");
  REQUIRE(loaded->displaySlug() == "my_world");
  REQUIRE(loaded->seed == 12345);
  REQUIRE(loaded->gameMode == 0);
  REQUIRE(loaded->lastPlayedTimestamp == meta.lastPlayedTimestamp);

  // Verify magic/version
  REQUIRE(loaded->magic == voxel::WorldMetadata::MAGIC);
  REQUIRE(loaded->version == voxel::WorldMetadata::CURRENT_VERSION);

  // Touch timestamp
  loaded->touch();
  REQUIRE(loaded->lastPlayedTimestamp >= meta.lastPlayedTimestamp);

  // Cleanup
  fs::remove_all(tmpDir);
}

TEST_CASE("WorldMetadata invalid file returns nullopt", "[save][metadata]") {
  auto tmpDir = fs::temp_directory_path() / "voxel_test_meta_bad";
  fs::remove_all(tmpDir);
  fs::create_directories(tmpDir);
  auto metaPath = tmpDir / "nonexistent.meta";

  auto result = voxel::WorldMetadata::read(metaPath);
  REQUIRE_FALSE(result.has_value());

  fs::remove_all(tmpDir);
}

TEST_CASE("WorldNamingService slug generation", "[save][naming]") {
  SECTION("simple name") {
    REQUIRE(voxel::WorldNamingService::generateSlug("My World") == "my_world");
  }
  SECTION("spaces and special chars") {
    REQUIRE(voxel::WorldNamingService::generateSlug("Hello!!! World?") == "hello_world");
  }
  SECTION("already safe") {
    REQUIRE(voxel::WorldNamingService::generateSlug("test_world_1") == "test_world_1");
  }
  SECTION("leading/trailing spaces") {
    REQUIRE(voxel::WorldNamingService::generateSlug("  spaces  ") == "spaces");
  }
  SECTION("only special chars") {
    REQUIRE(voxel::WorldNamingService::generateSlug("!!! ???") == "world");
  }
  SECTION("mixed case") {
    REQUIRE(voxel::WorldNamingService::generateSlug("MyCoolWorld") == "mycoolworld");
  }
  SECTION("numbers") {
    REQUIRE(voxel::WorldNamingService::generateSlug("World 123") == "world_123");
  }
}

TEST_CASE("WorldNamingService name validation", "[save][naming]") {
  SECTION("empty name") {
    auto err = voxel::WorldNamingService::validateName("");
    REQUIRE_FALSE(err.empty());
  }
  SECTION("too long") {
    std::string longName(61, 'a');
    auto err = voxel::WorldNamingService::validateName(longName);
    REQUIRE_FALSE(err.empty());
  }
  SECTION("no alphanumeric") {
    auto err = voxel::WorldNamingService::validateName("___");
    REQUIRE_FALSE(err.empty());
  }
  SECTION("valid name") {
    auto err = voxel::WorldNamingService::validateName("My World");
    REQUIRE(err.empty());
  }
  SECTION("valid short name") {
    auto err = voxel::WorldNamingService::validateName("a");
    REQUIRE(err.empty());
  }
}

TEST_CASE("WorldNamingService collision detection", "[save][naming]") {
  auto tmpDir = fs::temp_directory_path() / "voxel_test_naming";
  fs::remove_all(tmpDir);
  fs::create_directories(tmpDir);

  // Create a fake world directory with metadata
  auto worldDir = tmpDir / "existing_world";
  fs::create_directories(worldDir);
  auto meta = voxel::WorldMetadata::create("Existing World", "existing_world", 0, 0);
  REQUIRE(meta.write(worldDir / "world.meta"));

  voxel::WorldNamingService naming(tmpDir.string());

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
  auto tmpDir = fs::temp_directory_path() / "voxel_test_list";
  fs::remove_all(tmpDir);

  // Create multiple world directories with metadata
  auto createWorld = [&](const std::string& name, const std::string& slug,
                          uint32_t seed, int64_t timestamp) {
    auto dir = tmpDir / slug;
    fs::create_directories(dir);
    auto meta = voxel::WorldMetadata::create(name, slug, seed, 0);
    meta.lastPlayedTimestamp = timestamp;
    REQUIRE(meta.write(dir / "world.meta"));
  };

  // Create worlds with different timestamps
  createWorld("Alpha", "alpha", 100, 1000);
  createWorld("Beta", "beta", 200, 3000);
  createWorld("Gamma", "gamma", 300, 2000);

  voxel::WorldListService listService(tmpDir.string());

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
    auto emptyDir = fs::temp_directory_path() / "voxel_test_list_empty";
    fs::remove_all(emptyDir);
    fs::create_directories(emptyDir);
    voxel::WorldListService emptyService(emptyDir.string());
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
  auto tmpDir = fs::temp_directory_path() / "voxel_test_settings";
  fs::remove_all(tmpDir);
  fs::create_directories(tmpDir);
  auto dbPath = tmpDir / "settings.db";

  voxel::SettingsRepository repo(dbPath.string());

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
  auto tmpDir = fs::temp_directory_path() / "voxel_test_settings_persist";
  fs::remove_all(tmpDir);
  fs::create_directories(tmpDir);
  auto dbPath = tmpDir / "settings.db";

  // First session: write data
  {
    voxel::SettingsRepository repo(dbPath.string());
    repo.set("name", "test_value");
    repo.setInt("render_distance", 8);
    repo.setFloat("volume", 0.75f);
    repo.setBool("show_fps", false);
    // Destructor flushes and closes
  }

  // Second session: verify data persists
  {
    voxel::SettingsRepository repo(dbPath.string());
    REQUIRE(repo.get("name") == "test_value");
    REQUIRE(repo.getInt("render_distance") == 8);
    REQUIRE(repo.getFloat("volume") == 0.75f);
    REQUIRE(repo.getBool("show_fps") == false);
  }

  fs::remove_all(tmpDir);
}
