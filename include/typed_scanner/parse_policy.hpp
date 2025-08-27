#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
#include <string>
#include <vector>

namespace ts {

struct DatePolicy;
struct BoolPolicy;

struct ParsePolicy {
  // Behavior when a field fails to parse:
  // strict  -> error; lenient -> keep as string; null -> set null
  enum class OnError { Strict, Lenient, Null };

  OnError on_error = OnError::Null;
  DatePolicy* date_policy = nullptr;
  BoolPolicy* bool_policy = nullptr;

  // Numeric parse (fast_float in .cpp). Returns double for simplicity.
  std::optional<double> parse_number(std::string_view s) const;

  // Date parse (iso8601 fast path in .cpp). Value is milliseconds since epoch.
  std::optional<std::int64_t> parse_date(std::string_view s) const;

  // Boolean parse from a configured set (e.g., "true","false","1","0").
  std::optional<bool> parse_bool(std::string_view s) const;

  // Null test — leveraged by CSV null_values and JSONL policy.
  bool is_null_token(std::string_view s) const;
};

// A simple date policy; extended in .cpp
struct DatePolicy {
  // e.g. "iso8601"
  std::string mode = "iso8601";
};

// A simple bool policy
struct BoolPolicy {
  std::vector<std::string> true_tokens  = {"true","1","TRUE","True"};
  std::vector<std::string> false_tokens = {"false","0","FALSE","False"};
  bool case_sensitive = false;
};

}
