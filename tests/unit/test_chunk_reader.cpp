#include "typed_scanner/chunk_reader.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main(){
  const fs::path f = "tests/data/utf8.csv";
  if (!fs::exists(f)) { std::cerr << "[ERR] missing: " << f << "\n"; return 2; }

  ts::ChunkReader r(f.string(), {});
  uint64_t bytes = 0, lines = 0;
  bool ok = r.for_each_line([&](std::string_view s){
    bytes += s.size() + 1; // +1 approx for newline
    ++lines;
    return true;
  });
  if (!ok) { std::cerr << "[FAIL] chunk_reader aborted\n"; return 1; }
  if (lines < 2) { std::cerr << "[FAIL] expected multiple lines, got " << lines << "\n"; return 1; }
  uint64_t stat_size = fs::file_size(f);
  if (bytes == 0 || stat_size == 0) { std::cerr << "[FAIL] zero sizes\n"; return 1; }
  std::cout << "[PASS] lines="<<lines<<" bytes~="<<bytes<<" file_size="<<stat_size<<"\n";
  return 0;
}
