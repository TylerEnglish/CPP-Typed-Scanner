#pragma once
#include <string_view>
#include <vector>
#include <cstddef>

namespace ts {

class Arena {
public:
  explicit Arena(std::size_t cap_bytes = 0);
  void* alloc(std::size_t n);
  std::string_view copy(std::string_view s);

  // Reset head to zero; capacity stays (reuse buffer).
  void reset() noexcept;

  // Reset and optionally shrink capacity to `keep_capacity` bytes.
  void reset_and_shrink(std::size_t keep_capacity = 0);

  std::size_t used() const noexcept;
  std::size_t capacity() const noexcept;
  std::size_t high_water() const noexcept;

private:
  std::vector<char> buf_;
  std::size_t head_{0};
  std::size_t high_water_{0};
};

}