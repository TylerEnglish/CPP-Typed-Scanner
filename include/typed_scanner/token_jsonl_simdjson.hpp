#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ts {

class Arena;
struct RecordView;

struct JsonlConfig {
  bool   strict = true;                 // object-only in strict mode
  size_t cap_nested_value_bytes = 32 * 1024; // cap for arrays/objects raw storage
  bool   intern_keys = true;            // store keys in header arena (stable)
};

class JsonlTokenizer {
public:
  using RecordCallback = std::function<void(const RecordView&)>;

  JsonlTokenizer(const JsonlConfig& cfg, Arena& header_arena, Arena& row_arena);
  bool feed_line(std::string_view line, const RecordCallback& on_record);

  const std::vector<std::string_view>& header() const;
  const std::string& error() const { return err_; }

private:
  struct Impl; Impl* p_;
  std::string err_;
};

}
