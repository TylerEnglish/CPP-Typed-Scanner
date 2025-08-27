#include "typed_scanner/etag_state.hpp"

namespace ts {

void EtagState::set(std::string slug, std::string etag) {
  std::lock_guard<std::mutex> lk(mu_);
  map_[std::move(slug)] = std::move(etag);
}

std::optional<std::string> EtagState::get(std::string_view slug) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = map_.find(std::string(slug));
  if (it == map_.end()) return std::nullopt;
  return it->second;
}

bool EtagState::matches(std::string_view slug, std::string_view etag) const {
  auto v = get(slug);
  return v.has_value() && v->compare(std::string(etag)) == 0;
}

}
