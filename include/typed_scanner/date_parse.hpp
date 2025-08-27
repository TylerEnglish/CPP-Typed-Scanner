#pragma once
#include <cstdint>
#include <optional>
#include <string_view>

namespace ts {

// Fast parse of a limited ISO-8601 subset: returns epoch millis on success.
std::optional<std::int64_t> parse_iso8601_ms(std::string_view s);

}
