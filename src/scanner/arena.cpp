#include "typed_scanner/arena.hpp"
#include <algorithm>
#include <cstring>

namespace ts {

Arena::Arena(std::size_t cap_bytes) : buf_(cap_bytes), head_(0), high_water_(0) {}

void* Arena::alloc(std::size_t n) {
  if (head_ + n > buf_.size()) {
    std::size_t need = head_ + n;
    std::size_t grow = std::max(need, buf_.size() + buf_.size() / 2 + 1);
    buf_.resize(grow);
  }
  void* p = buf_.data() + head_;
  head_ += n;
  if (head_ > high_water_) high_water_ = head_;
  return p;
}

std::string_view Arena::copy(std::string_view s) {
  if (s.empty()) return {};
  void* p = alloc(s.size());
  std::memcpy(p, s.data(), s.size());
  return std::string_view(static_cast<const char*>(p), s.size());
}

void Arena::reset() noexcept { head_ = 0; }

void Arena::reset_and_shrink(std::size_t keep_capacity) {
  head_ = 0;
  if (keep_capacity < buf_.size()) buf_.resize(keep_capacity);
  if (high_water_ > buf_.size()) high_water_ = buf_.size();
}

std::size_t Arena::used() const noexcept { return head_; }
std::size_t Arena::capacity() const noexcept { return buf_.size(); }
std::size_t Arena::high_water() const noexcept { return high_water_; }

}
