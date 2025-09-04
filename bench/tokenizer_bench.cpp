#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "typed_scanner/chunk_reader.hpp"
#include "typed_scanner/token_csv_fsm.hpp"
#include "typed_scanner/record_view.hpp"
#include "typed_scanner/arena.hpp"

#if defined(TS_ENABLE_JSONL) && TS_ENABLE_JSONL
  #include "typed_scanner/token_jsonl_simdjson.hpp"
  #define TS_HAS_JSONL 1
#else
  #define TS_HAS_JSONL 0
#endif

namespace fs = std::filesystem;
using clk = std::chrono::steady_clock;

static std::string make_synth_csv(std::size_t rows, std::size_t cols) {
  fs::path p = fs::temp_directory_path() / "ts_bench_synth.csv";
  std::ofstream out(p, std::ios::binary);
  // header
  for (size_t c = 0; c < cols; ++c) { out << "col" << c; if (c+1<cols) out << ","; }
  out << "\n";
  // rows
  for (size_t r = 0; r < rows; ++r) {
    for (size_t c = 0; c < cols; ++c) {
      out << (r%10) << "." << (c*37%1000);
      if (c+1<cols) out << ",";
    }
    out << "\n";
  }
  out.flush();
  return p.string();
}

static std::string make_synth_jsonl(std::size_t rows, std::size_t cols) {
  fs::path p = fs::temp_directory_path() / "ts_bench_synth.jsonl";
  std::ofstream out(p, std::ios::binary);
  for (size_t r = 0; r < rows; ++r) {
    out << "{";
    for (size_t c = 0; c < cols; ++c) {
      out << "\"k" << c << "\":\"" << (r%10) << "." << (c*37%1000) << "\"";
      if (c+1<cols) out << ",";
    }
    out << "}\n";
  }
  out.flush();
  return p.string();
}

struct Args {
  std::string csv_path;        // if empty -> synth
  std::string jsonl_path;      // if empty -> synth (when JSONL enabled)
  std::size_t rows = 200'000;  // for synth
  std::size_t cols = 8;        // for synth
  int iters = 3;
};

static Args parse_args(int argc, char** argv) {
  Args a;
  for (int i=1;i<argc;++i){
    std::string s(argv[i]);
    auto eq = s.find('=');
    auto key = s.substr(0, eq);
    auto val = (eq==std::string::npos) ? "" : s.substr(eq+1);
    if (key=="--csv") a.csv_path = val;
    else if (key=="--jsonl") a.jsonl_path = val;
    else if (key=="--rows") a.rows = std::stoull(val);
    else if (key=="--cols") a.cols = std::stoull(val);
    else if (key=="--iters") a.iters = std::stoi(val);
    else if (key=="--help" || key=="-h") {
      std::cout <<
        "Usage: ts_bench_tokenizer [--csv=path] [--jsonl=path] [--rows=N] [--cols=M] [--iters=K]\n"
        "If paths are omitted, synthetic CSV/JSONL are generated.\n";
      std::exit(0);
    }
  }
  return a;
}

static void bench_csv(const std::string& path, int iters) {
  std::cout << "\n[CSV] file=" << path << " iters=" << iters << "\n";
  for (int k=1;k<=iters;++k) {
    ts::Arena header(64*1024), rows(16*1024*1024);
    ts::CsvConfig cfg; // header=true by default
    ts::CsvFsm csv(cfg, header, rows);
    ts::ChunkReader rd(path, {});
    std::uint64_t nrec=0;

    auto t0 = clk::now();
    rd.for_each_line([&](std::string_view s){
      (void)csv.feed(s, [&](const ts::RecordView&){ ++nrec; if((nrec%20000)==0) rows.reset();});
      return true;
    });
    csv.finish([&](const ts::RecordView&){ ++nrec; });
    auto t1 = clk::now();

    const double sec = std::chrono::duration<double>(t1-t0).count();
    const double mib = rd.bytes_read() / (1024.0*1024.0);
    std::cout << "  iter " << k
              << ": rows=" << nrec
              << " bytes=" << rd.bytes_read()
              << " time=" << sec << "s"
              << "  throughput=" << (mib/sec) << " MiB/s"
              << "  rows/s=" << (nrec/sec) << "\n";
  }
}

#if TS_HAS_JSONL
static void bench_jsonl(const std::string& path, int iters) {
  std::cout << "\n[JSONL] file=" << path << " iters=" << iters << "\n";
  for (int k=1;k<=iters;++k) {
    ts::Arena header(64*1024), rows(16*1024*1024);
    ts::JsonlConfig cfg; // tokenizer is strict in headers
    ts::JsonlTokenizer tok(cfg, header, rows);
    ts::ChunkReader rd(path, {});
    std::uint64_t nrec=0;

    auto t0 = clk::now();
    rd.for_each_line([&](std::string_view s){
      (void)tok.feed_line(s, [&](const ts::RecordView&){ ++nrec; if((nrec%40000)==0) rows.reset();});
      return true;
    });
    auto t1 = clk::now();

    const double sec = std::chrono::duration<double>(t1-t0).count();
    const double mib = rd.bytes_read() / (1024.0*1024.0);
    std::cout << "  iter " << k
              << ": rows=" << nrec
              << " bytes=" << rd.bytes_read()
              << " time=" << sec << "s"
              << "  throughput=" << (mib/sec) << " MiB/s"
              << "  rows/s=" << (nrec/sec) << "\n";
  }
}
#endif

int main(int argc, char** argv){
  Args a = parse_args(argc, argv);

  std::string csv = a.csv_path;
  if (csv.empty() || !fs::exists(csv)) csv = make_synth_csv(a.rows, a.cols);
  bench_csv(csv, a.iters);

#if TS_HAS_JSONL
  std::string jsonl = a.jsonl_path;
  if (jsonl.empty() || !fs::exists(jsonl)) jsonl = make_synth_jsonl(a.rows, a.cols);
  bench_jsonl(jsonl, a.iters);
#else
  std::cout << "\n[JSONL] disabled at build time (TS_ENABLE_JSONL=OFF)\n";
#endif
  return 0;
}
