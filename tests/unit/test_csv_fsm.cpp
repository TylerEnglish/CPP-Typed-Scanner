#include "typed_scanner/token_csv_fsm.hpp"
#include "typed_scanner/chunk_reader.hpp"
#include "typed_scanner/arena.hpp"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main(){
  const fs::path f = "tests/data/edge_delimiters.csv";
  if (!fs::exists(f)) { std::cerr << "[ERR] missing: " << f << "\n"; return 2; }

  ts::Arena header(64*1024), rows(8*1024*1024);
  ts::CsvConfig cfg; // header=true
  ts::CsvFsm csv(cfg, header, rows);

  ts::ChunkReader r(f.string(), {});
  uint64_t n=0;
  bool ok = r.for_each_line([&](std::string_view s){
    if (!csv.feed(s, [&](const ts::RecordView&){ ++n; })) return false;
    return true;
  });
  ok &= csv.finish([&](const ts::RecordView&){ ++n; });

  if (!ok) { std::cerr << "[FAIL] csv_fsm error: " << csv.error() << "\n"; return 1; }
  if (n != 6) { std::cerr << "[FAIL] expected 6 data rows, got " << n << "\n"; return 1; }
  std::cout << "[PASS] parsed " << n << " rows\n";
  return 0;
}
