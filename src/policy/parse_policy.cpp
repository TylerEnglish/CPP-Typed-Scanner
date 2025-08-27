#include "typed_scanner/parse_policy.hpp"
#include "typed_scanner/date_parse.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <string_view>
#include <fast_float/fast_float.h>

namespace ts {

static bool ieq(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
  return true;
}

std::optional<double> ParsePolicy::parse_number(std::string_view s) const {
  double out;
  auto [ptr, ec] = fast_float::from_chars(s.data(), s.data() + s.size(), out);
  if (ec != std::errc() || ptr != s.data() + s.size()) {
    if (on_error == OnError::Strict) return std::nullopt;
    if (on_error == OnError::Null)   return std::nullopt;
    return std::nullopt; // lenient -> keep as string upstream
  }
  return out;
}

std::optional<std::int64_t> ParsePolicy::parse_date(std::string_view s) const {
  if (!date_policy) return std::nullopt;
  if (date_policy->mode == "iso8601") return parse_iso8601_ms(s);
  return std::nullopt;
}

std::optional<bool> ParsePolicy::parse_bool(std::string_view s) const {
  if (!bool_policy) return std::nullopt;
  for (const auto& t : bool_policy->true_tokens) {
    if (bool_policy->case_sensitive ? (s == t) : ieq(s, t)) return true;
  }
  for (const auto& f : bool_policy->false_tokens) {
    if (bool_policy->case_sensitive ? (s == f) : ieq(s, f)) return false;
  }
  return std::nullopt;
}

// Minimal null heuristic: empty string or explicit tokens in bool policy's sets
bool ParsePolicy::is_null_token(std::string_view s) const {
  if (s.empty()) return true;
  static constexpr std::string_view nulls[] = {"null","NULL","NaN","","NA"};
  for (auto n : nulls) if (s == n) return true;
  return false;
}

}
