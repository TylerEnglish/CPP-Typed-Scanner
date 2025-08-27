#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ts {

class Arena;
struct RecordView;

struct CsvConfig {
  char delimiter = ',';
  char quote     = '"';
  bool header    = true;
};

class CsvFsm {
public:
  using RecordCallback = std::function<void(const RecordView&)>;

  // Split arenas: headers live in `header_arena`, row fields in `row_arena`.
  CsvFsm(const CsvConfig& cfg, Arena& header_arena, Arena& row_arena);
  ~CsvFsm();

  bool feed(std::string_view chunk_line, const RecordCallback& on_record);
  bool finish(const RecordCallback& on_record);
  const std::vector<std::string_view>& header() const;
  const std::string& error() const { return err_; }
  std::uint64_t rows() const { return rows_; }

private:
  struct Impl; Impl* p_;
  std::uint64_t rows_{0};
  std::string err_;
};

}
