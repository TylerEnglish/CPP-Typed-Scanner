#include "typed_scanner/chunk_reader.hpp"
#include "typed_scanner/token_csv_fsm.hpp"
#include "typed_scanner/token_jsonl_simdjson.hpp"
#include "typed_scanner/record_view.hpp"
#include "typed_scanner/arena.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <cctype>
#include <cstring>

namespace fs = std::filesystem;

static bool ieq_ext(const std::string& s, const char* ext) {
  if (s.size() != std::strlen(ext)) return false;
  for (size_t i = 0; i < s.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(ext[i]))) return false;
  return true;
}

static bool expected_ok_for(const fs::path& p) {
  const std::string n = p.filename().string();
  if (n.find("bad") != std::string::npos) return false;
  if (n.find("malformed") != std::string::npos) return false;
  if (n.find("multi_line") != std::string::npos) return false;
  return true;
}

static std::string show_snippet(std::string_view s, size_t max = 180) {
  std::string out; out.reserve(s.size());
  auto push_hex = [&](unsigned char c){
    const char *hex = "0123456789ABCDEF";
    out += "\\x"; out += hex[c>>4]; out += hex[c&0xF];
  };
  for (unsigned char c : s) {
    if (c == '\n') { out += "\\n"; }
    else if (c == '\r') { out += "\\r"; }
    else if (c == '\t') { out += "\\t"; }
    else if (c < 0x20 || c == 0x7f) { push_hex(c); }
    else { out.push_back(static_cast<char>(c)); }
    if (out.size() >= max) { out += "…"; break; }
  }
  return out;
}

struct Res {
  bool ok{true};
  uint64_t rows{0};
  uint64_t bytes{0};
  uint64_t fail_line{0};
  std::string fail_snippet;
  std::string err;
};

static Res run_csv(const fs::path& f){
  Res r;
  ts::Arena header(64 * 1024), rows(16 * 1024 * 1024);
  ts::ChunkReader reader(f.string(), {});
  ts::CsvConfig cfg; // header=true by default
  ts::CsvFsm csv(cfg, header, rows);

  uint64_t line_no = 0;
  bool ok = true;
  auto on_rec = [&](const ts::RecordView&){ ++r.rows; if ((r.rows % 10000)==0) rows.reset(); };

  ok = reader.for_each_line([&](std::string_view s){
    ++line_no;
    bool step = csv.feed(s, on_rec);
    if (!step && ok) {
      r.fail_line = line_no;
      r.fail_snippet = show_snippet(s);
      r.err = csv.error();
    }
    ok &= step;
    return true;
  });
  ok &= csv.finish(on_rec);

  r.ok = ok; r.bytes = reader.bytes_read();
  if (!ok && r.err.empty()) r.err = csv.error();
  return r;
}

static Res run_jsonl(const fs::path& f){
  Res r;
  ts::Arena header(64 * 1024), rows(16 * 1024 * 1024);
  ts::ChunkReader reader(f.string(), {});
  ts::JsonlConfig cfg; // strict=true in your headers
  ts::JsonlTokenizer tok(cfg, header, rows);

  uint64_t line_no = 0;
  bool ok = true;

  ok = reader.for_each_line([&](std::string_view s){
    ++line_no;
    bool step = tok.feed_line(s, [&](const ts::RecordView&){ ++r.rows; });
    if (!step && ok) {
      r.fail_line = line_no;
      r.fail_snippet = show_snippet(s);
      r.err = tok.error();
    }
    ok &= step;
    return true;
  });

  r.ok = ok; r.bytes = reader.bytes_read();
  if (!ok && r.err.empty()) r.err = tok.error();
  return r;
}

int main(int argc, char** argv){
  fs::path dir = (argc > 1) ? fs::path(argv[1]) : fs::path("tests/data");
  if (!fs::exists(dir)) {
    std::cerr << "[ERR] fixtures dir not found: " << dir << "\n";
    return 2;
  }

  size_t total=0, passed=0, failed=0;
  for (auto& it : fs::directory_iterator(dir)) {
    if (!it.is_regular_file()) continue;
    const fs::path p = it.path();
    const std::string e = p.extension().string();

    Res r;
    if (ieq_ext(e, ".csv"))          r = run_csv(p);
    else if (ieq_ext(e, ".jsonl") || ieq_ext(e, ".ndjson")) r = run_jsonl(p);
    else continue;

    const bool expect_ok = expected_ok_for(p);
    const bool verdict = (r.ok == expect_ok);

    ++total; verdict ? ++passed : ++failed;

    if (verdict) {
      std::cout << "[PASS] " << p.filename().string()
                << "  rows=" << r.rows
                << "  bytes=" << r.bytes
                << "  expected_ok=" << (expect_ok?"true":"false") << "\n";
    } else {
      std::cout << "[FAIL] " << p.filename().string()
                << "  rows=" << r.rows
                << "  bytes=" << r.bytes
                << "  expected_ok=" << (expect_ok?"true":"false")
                << "  actual_ok=" << (r.ok?"true":"false") << "\n";
      if (!r.err.empty())
        std::cout << "       error: " << r.err << "\n";
      if (r.fail_line)
        std::cout << "       at line " << r.fail_line
                  << ": " << r.fail_snippet << "\n";
    }
  }

  std::cout << "\nSummary: total=" << total << " passed=" << passed << " failed=" << failed << "\n";
  return failed == 0 ? 0 : 1;
}
