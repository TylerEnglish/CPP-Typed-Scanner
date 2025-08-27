#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <mutex>
#include <optional>

namespace ts {

// Keeps track of (slug -> etag) to enable idempotent rebuilds.
class EtagState {
public:
  void set(std::string slug, std::string etag);
  std::optional<std::string> get(std::string_view slug) const;
  bool matches(std::string_view slug, std::string_view etag) const;

private:
  mutable std::mutex mu_;
  std::unordered_map<std::string, std::string> map_;
};

}
