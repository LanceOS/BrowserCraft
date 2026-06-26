#include "WorldNamingService.hpp"
#include "WorldMetadata.hpp"
#include <algorithm>
#include <cctype>

namespace terrain {

WorldNamingService::WorldNamingService(std::filesystem::path saveDir)
  : m_saveDir(std::move(saveDir))
{}

auto WorldNamingService::isNameTaken(std::string_view displayName) const -> bool {
  if (!std::filesystem::exists(m_saveDir)) return false;

  // Check each world directory's metadata for a matching display name
  for (const auto& entry : std::filesystem::directory_iterator(m_saveDir)) {
    if (!entry.is_directory()) continue;
    auto metaPath = entry.path() / "world.meta";
    if (!std::filesystem::exists(metaPath)) continue;

    auto meta = WorldMetadata::read(metaPath);
    if (meta && meta->displayName() == displayName) {
      return true;
    }
  }
  return false;
}

auto WorldNamingService::isSlugTaken(std::string_view slug) const -> bool {
  if (!std::filesystem::exists(m_saveDir)) return false;

  auto dirPath = m_saveDir / slug;
  return std::filesystem::exists(dirPath);
}

auto WorldNamingService::generateSlug(std::string_view displayName) -> std::string {
  std::string slug;
  slug.reserve(displayName.size());

  for (unsigned char ch : displayName) {
    if (std::isalnum(ch)) {
      slug += static_cast<char>(std::tolower(ch));
    } else if (ch == ' ' || ch == '-' || ch == '_') {
      slug += '_';
    }
    // Skip other characters
  }

  // Trim leading/trailing underscores
  auto trimStart = slug.find_first_not_of('_');
  if (trimStart == std::string::npos) return "world"; // all underscores, empty-ish
  auto trimEnd = slug.find_last_not_of('_');
  slug = slug.substr(trimStart, trimEnd - trimStart + 1);

  if (slug.empty()) slug = "world";
  return slug;
}

auto WorldNamingService::nextAvailableSlug(std::string_view displayName) const -> std::string {
  auto base = generateSlug(displayName);
  if (!isSlugTaken(base)) return base;

  // Append numeric suffix until we find a free slug
  for (int32_t suffix = 2; suffix < 9999; ++suffix) {
    auto candidate = base + "_" + std::to_string(suffix);
    if (!isSlugTaken(candidate)) return candidate;
  }

  // Extremely unlikely fallback with timestamp
  return base + "_" + std::to_string(
    std::chrono::system_clock::now().time_since_epoch().count());
}

auto WorldNamingService::validateName(std::string_view displayName) -> std::string {
  if (displayName.empty()) return "World name cannot be empty";

  if (displayName.size() > 60) return "World name is too long (max 60 characters)";

  // Must contain at least one alphanumeric character
  bool hasAlnum = std::any_of(displayName.begin(), displayName.end(),
    [](unsigned char c) { return std::isalnum(c); });
  if (!hasAlnum) return "World name must contain at least one letter or number";

  return {}; // valid
}

} // namespace terrain
