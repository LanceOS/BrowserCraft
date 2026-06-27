#pragma once

#include <string>
#include <unordered_map>
#include <filesystem>
#include <memory>

struct sqlite3;

namespace terrain {

/// Lightweight SQLite-backed key-value store for user settings.
/// Thread-safe: uses WAL mode with a single mutex for writes.
class SettingsRepository {
public:
  explicit SettingsRepository(std::filesystem::path dbPath);
  ~SettingsRepository();

  SettingsRepository(const SettingsRepository&) = delete;
  SettingsRepository& operator=(const SettingsRepository&) = delete;

  // ---- Read ----

  /// Get a string setting. Returns defaultVal if not found.
  [[nodiscard]] auto get(const std::string& key, const std::string& defaultVal = {}) const -> std::string;

  /// Get an integer setting. Returns defaultVal if not found.
  [[nodiscard]] auto getInt(const std::string& key, int32_t defaultVal = 0) const -> int32_t;

  /// Get a float setting. Returns defaultVal if not found.
  [[nodiscard]] auto getFloat(const std::string& key, float defaultVal = 0.0f) const -> float;

  /// Get a boolean setting. Returns defaultVal if not found.
  [[nodiscard]] auto getBool(const std::string& key, bool defaultVal = false) const -> bool;

  // ---- Write ----

  /// Set a string value.
  void set(const std::string& key, const std::string& value);

  /// Set an integer value (stored as string).
  void setInt(const std::string& key, int32_t value);

  /// Set a float value (stored as string).
  void setFloat(const std::string& key, float value);

  /// Set a boolean value (stored as "1" or "0").
  void setBool(const std::string& key, bool value);

  /// Check if a key exists.
  [[nodiscard]] auto has(const std::string& key) const -> bool;

  /// Remove a key.
  void remove(const std::string& key);

  /// Remove all settings.
  void clear();

  /// Persist any pending writes to disk.
  void flush();

private:
  auto openDb() -> bool;
  void ensureTable();
  auto query(const std::string& key) const -> std::string;

  std::filesystem::path m_dbPath;
  sqlite3* m_db = nullptr;
  mutable std::mutex m_mutex;
};

} // namespace terrain
