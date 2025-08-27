#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ts {

enum class FileFormat { CSV, JSONL, Unknown };

// Join and normalize a/b to an fs::path.
std::filesystem::path join(const std::filesystem::path& a,
                           const std::filesystem::path& b);

// Ensure parent directories exist; returns false on error.
bool ensure_parent_dirs(const std::filesystem::path& p);

// Guess format from extension (.csv | .jsonl | .ndjson).
FileFormat detect_format(std::string_view path);

// Slug generation per config: "hashprefix", "basename", or "keypath".
// We expose a utility that callers give mode + length.
std::string make_slug(std::string_view key, std::string_view mode, int len);

// Hash helper (stable) used by slug.
std::string hex_hash_prefix(std::string_view data, int len);

}
