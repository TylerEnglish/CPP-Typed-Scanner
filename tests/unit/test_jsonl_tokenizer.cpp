#include "typed_scanner/token_jsonl_simdjson.hpp"
#include "typed_scanner/chunk_reader.hpp"
#include "typed_scanner/arena.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

static size_t count_lines(const fs::path& f) {
  std::ifstream in(f); size_t n=0; std::string s; while (std::getline(in,s)) ++n; return n;
}

int main(){
  const fs::path f = "data/samples/simple.jsonl";
  if (!fs::exists(f)) { std::cerr << "[ERR] missing: " << f << "\n"; return 2; }

  ts::Arena hdr(64*1024), rows(8*1024*1024);
  ts::JsonlConfig cfg; // strict=true
  ts::JsonlTokenizer tok(cfg, hdr, rows);

  ts::ChunkReader r(f.string(), {});
  uint64_t n=0;
  bool ok = r.for_each_line([&](std::string_view s){
    if (!tok.feed_line(s, [&](const ts::RecordView&){ ++n; })) return false;
    return true;
  });
  if (!ok) { std::cerr << "[FAIL] tokenizer error: " << tok.error() << "\n"; return 1; }

  size_t expect = count_lines(f);
  if (n != expect) { std::cerr << "[FAIL] rows="<<n<<" expect="<<expect<<"\n"; return 1; }
  std::cout << "[PASS] jsonl rows="<<n<<"\n";
  return 0;
}
