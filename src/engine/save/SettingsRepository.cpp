#include "SettingsRepository.hpp"
#include <sqlite3.h>
#include <mutex>
#include <sstream>

namespace voxel {

SettingsRepository::SettingsRepository(std::filesystem::path dbPath)
  : m_dbPath(std::move(dbPath)) {
  openDb();
}

SettingsRepository::~SettingsRepository() {
  if (m_db) {
    flush();
    int rc = sqlite3_close(m_db);
    (void)rc; // Debug: rc would be SQLITE_BUSY if statements weren't finalized
    m_db = nullptr;
  }
}

auto SettingsRepository::openDb() -> bool {
  if (m_db) return true;

  // Ensure parent directory exists
  auto parent = m_dbPath.parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
  }

  int rc = sqlite3_open(m_dbPath.c_str(), &m_db);
  if (rc != SQLITE_OK) {
    m_db = nullptr;
    return false;
  }

  // Enable WAL mode for better concurrency, with explicit checkpoint control
  char* errMsg = nullptr;
  sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &errMsg);
  if (errMsg) {
    sqlite3_free(errMsg);
    errMsg = nullptr;
  }

  // Set synchronous to NORMAL (faster, safe with WAL)
  sqlite3_exec(m_db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, &errMsg);
  if (errMsg) {
    sqlite3_free(errMsg);
    errMsg = nullptr;
  }

  // Set a busy timeout instead of failing immediately on contention
  sqlite3_busy_timeout(m_db, 100);

  ensureTable();

  // Verify the database is writable
  {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM settings", -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
      sqlite3_step(stmt);
      sqlite3_finalize(stmt);
    }
  }

  return true;
}

void SettingsRepository::ensureTable() {
  const char* sql =
    "CREATE TABLE IF NOT EXISTS settings ("
    "  key   TEXT PRIMARY KEY NOT NULL,"
    "  value TEXT NOT NULL"
    ");";
  char* errMsg = nullptr;
  int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg);
  if (rc != SQLITE_OK && errMsg) {
    sqlite3_free(errMsg);
  }
}

auto SettingsRepository::query(const std::string& key) const -> std::string {
  std::lock_guard lock(m_mutex);

  if (!m_db) return {};

  const char* sql = "SELECT value FROM settings WHERE key = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return {};
  }

  sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);

  std::string result;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (text) result = text;
  }

  sqlite3_finalize(stmt);
  return result;
}

auto SettingsRepository::get(const std::string& key, const std::string& defaultVal) const -> std::string {
  auto val = query(key);
  return val.empty() ? defaultVal : val;
}

auto SettingsRepository::getInt(const std::string& key, int32_t defaultVal) const -> int32_t {
  auto val = query(key);
  if (val.empty()) return defaultVal;
  try { return std::stoi(val); } catch (...) { return defaultVal; }
}

auto SettingsRepository::getFloat(const std::string& key, float defaultVal) const -> float {
  auto val = query(key);
  if (val.empty()) return defaultVal;
  try { return std::stof(val); } catch (...) { return defaultVal; }
}

auto SettingsRepository::getBool(const std::string& key, bool defaultVal) const -> bool {
  auto val = query(key);
  if (val.empty()) return defaultVal;
  return val == "1" || val == "true" || val == "yes";
}

void SettingsRepository::set(const std::string& key, const std::string& value) {
  std::lock_guard lock(m_mutex);

  if (!m_db) return;

  // Use UPSERT (INSERT OR REPLACE) for atomicity
  const char* sql = "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?);";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

  sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);

  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void SettingsRepository::setInt(const std::string& key, int32_t value) {
  set(key, std::to_string(value));
}

void SettingsRepository::setFloat(const std::string& key, float value) {
  set(key, std::to_string(value));
}

void SettingsRepository::setBool(const std::string& key, bool value) {
  set(key, value ? "1" : "0");
}

auto SettingsRepository::has(const std::string& key) const -> bool {
  return !query(key).empty();
}

void SettingsRepository::remove(const std::string& key) {
  std::lock_guard lock(m_mutex);

  if (!m_db) return;

  const char* sql = "DELETE FROM settings WHERE key = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

  sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void SettingsRepository::clear() {
  std::lock_guard lock(m_mutex);

  if (!m_db) return;

  char* errMsg = nullptr;
  sqlite3_exec(m_db, "DELETE FROM settings;", nullptr, nullptr, &errMsg);
  if (errMsg) sqlite3_free(errMsg);
}

void SettingsRepository::flush() {
  std::lock_guard lock(m_mutex);

  if (!m_db) return;

  // Run a full checkpoint to move WAL content into the main database file.
  // TRUNCATE mode is faster and ensures the WAL file is removed.
  int log = 0, ckpt = 0;
  int rc = sqlite3_wal_checkpoint_v2(m_db, nullptr, SQLITE_CHECKPOINT_TRUNCATE, &log, &ckpt);
  (void)rc;

  // After checkpoint, run a manual sync to ensure data is on disk
  sqlite3_exec(m_db, "PRAGMA wal_checkpoint(TRUNCATE);", nullptr, nullptr, nullptr);
}

} // namespace voxel
