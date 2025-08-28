#include "typed_scanner/http_server.hpp"
#include "typed_scanner/path_utils.hpp"
#include "typed_scanner/chunk_reader.hpp"
#include "typed_scanner/token_csv_fsm.hpp"
#include "typed_scanner/token_jsonl_simdjson.hpp"
#include "typed_scanner/arena.hpp"
#include "typed_scanner/run_json.hpp"
#include "typed_scanner/artifact_writer.hpp"
#include "typed_scanner/record_view.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Cli {
  int port = 8080;
  std::string artifact_root = "artifacts/typed-scanner";
  std::string slug_mode = "hashprefix"; // hashprefix|basename|keypath
  int slug_len = 8;
  bool scan_samples = false;
  bool serve_only = false;
  std::vector<std::string> scans; // explicit file paths
};

Cli parse_cli(int argc, char** argv) {
  Cli c;
  for (int i = 1; i < argc; ++i) {
    std::string a(argv[i]);
    auto eat = [&](const char* pfx, std::string* out){
      if (a.rfind(pfx, 0) == 0) { *out = a.substr(std::string(pfx).size()); return true; }
      return false;
    };
    auto eat_i = [&](const char* pfx, int* out){
      if (a.rfind(pfx, 0) == 0) { *out = std::stoi(a.substr(std::string(pfx).size())); return true; }
      return false;
    };
    if (eat_i("--port=", &c.port)) continue;
    if (eat("--artifact-root=", &c.artifact_root)) continue;
    if (eat("--slug-mode=", &c.slug_mode)) continue;
    if (eat_i("--slug-len=", &c.slug_len)) continue;
    if (a == "--scan-samples") { c.scan_samples = true; continue; }
    if (a == "--serve-only")   { c.serve_only   = true; continue; }
    if (a == "--scan" && i+1 < argc) { c.scans.push_back(argv[++i]); continue; }
    if (a.rfind("--scan=",0)==0) { c.scans.push_back(a.substr(7)); continue; }
    if (a == "-h" || a == "--help") {
      std::cout <<
        "Usage: typed-scanner [--port=N] [--artifact-root=DIR]\n"
        "                     [--slug-mode=hashprefix|basename|keypath] [--slug-len=N]\n"
        "                     [--scan <file>|--scan=<file>] [--scan-samples] [--serve-only]\n";
      std::exit(0);
    }
  }
  return c;
}

std::string make_slug_for(const std::string& path, const std::string& mode, int len) {
  // For hashprefix mode, hash the full absolute path to be stable in examples.
  std::string key = (mode == "hashprefix")
      ? std::filesystem::weakly_canonical(std::filesystem::path(path)).string()
      : path;
  return ts::make_slug(key, mode, len);
}

int scan_one_file(const std::string& filepath,
                  const std::string& artifact_root,
                  const std::string& slug_mode,
                  int slug_len) {
  namespace ch = std::chrono;
  const auto t0 = ch::steady_clock::now();

  // --- choose format
  ts::FileFormat fmt = ts::detect_format(filepath);
  if (fmt == ts::FileFormat::Unknown) {
    std::cerr << "[scan] skip unsupported: " << filepath << "\n";
    return 0;
  }

  // --- arenas
  ts::Arena header_arena(64 * 1024);
  ts::Arena row_arena(16 * 1024 * 1024);

  // --- reader
  ts::ChunkReader::Config rcfg;
  ts::ChunkReader reader(filepath, rcfg);

  // --- counters/series
  std::uint64_t rows = 0;
  std::uint64_t fields_total = 0;

  // record callback (counts rows/fields and resets row arena periodically)
  auto on_record = [&](const ts::RecordView& rv){
    ++rows;
    if (rv.fields()) fields_total += rv.fields()->size();
    if ((rows % 10000) == 0) row_arena.reset();
  };

  // --- tokenize
  bool ok = true;
  if (fmt == ts::FileFormat::CSV) {
    ts::CsvConfig ccfg; // header=true default
    ts::CsvFsm csv(ccfg, header_arena, row_arena);
    ok = reader.for_each_line([&](std::string_view line){ ok &= csv.feed(line, on_record); });
    ok &= csv.finish(on_record);
    if (!ok) std::cerr << "[scan] CSV error: " << csv.error() << "\n";
  } else if (fmt == ts::FileFormat::JSONL) {
    ts::JsonlConfig jcfg; // strict=true; keys interned
    ts::JsonlTokenizer jtok(jcfg, header_arena, row_arena);
    ok = reader.for_each_line([&](std::string_view line){ ok &= jtok.feed_line(line, on_record); });
    if (!ok) std::cerr << "[scan] JSONL error: " << jtok.error() << "\n";
  }

  const auto t1 = ch::steady_clock::now();
  const double wall_ms = ch::duration<double, std::milli>(t1 - t0).count();

  const std::uint64_t bytes = reader.bytes_read();
  const double mb = bytes / (1024.0 * 1024.0);
  const double sec = wall_ms / 1000.0;
  const double throughput_mb_s = sec > 0.0 ? (mb / sec) : 0.0;
  const double rows_per_s = sec > 0.0 ? (rows / sec) : 0.0;

  // --- run.json payload (fill what we have; rest can be zero/empty)
  ts::RunJsonPayload p{};
  p.rows = rows;
  p.bytes = bytes;
  p.wall_time_ms = wall_ms;
  p.throughput_mb_s = throughput_mb_s;
  p.tokens_per_sec = rows_per_s;           // treat "tokens" ~ rows for MVP
  p.allocs_per_sec = 0.0;                  // not measured here
  p.p50_ms = 0.0; p.p95_ms = 0.0;          // not measured here

  p.filename = filepath;
  p.content_type = (fmt == ts::FileFormat::CSV) ? "text/csv" : "application/x-ndjson";
  p.etag = ""; // optional; can add later
  std::error_code fec;
  p.file_size = std::filesystem::file_size(filepath, fec);

  // can also add stage_times, errors_by_field, series if you have them

  std::string run_json = ts::RunJsonWriter::to_json(p);

  // --- write artifacts
  const std::string slug = make_slug_for(filepath, slug_mode, slug_len);
  std::string err;
  if (!ts::write_report_dir(artifact_root, slug, run_json, &err)) {
    std::cerr << "[scan] write_report_dir failed: " << err << "\n";
    return 2;
  }

  std::cout << "[scan] ok: " << filepath
            << " → artifacts/typed-scanner/" << slug << "/report.html\n";
  return ok ? 0 : 3;
}

void scan_samples_if_requested(const std::string& artifact_root,
                               const std::string& slug_mode,
                               int slug_len) {
  const std::filesystem::path samples = "data/samples";
  if (!std::filesystem::exists(samples)) return;
  for (auto& e : std::filesystem::directory_iterator(samples)) {
    if (!e.is_regular_file()) continue;
    const std::string path = e.path().string();
    auto fmt = ts::detect_format(path);
    if (fmt == ts::FileFormat::Unknown) continue;
    (void)scan_one_file(path, artifact_root, slug_mode, slug_len);
  }
}

}

int main(int argc, char** argv) {
  auto cli = parse_cli(argc, argv);

  bool did_any_scan = false;

  if (!cli.serve_only) {
    // explicit scans
    for (const auto& f : cli.scans) {
      did_any_scan = true;
      (void)scan_one_file(f, cli.artifact_root, cli.slug_mode, cli.slug_len);
    }
    // sample bundle
    if (cli.scan_samples) {
      did_any_scan = true;
      scan_samples_if_requested(cli.artifact_root, cli.slug_mode, cli.slug_len);
    }
  }

  // If we performed any scans, exit now (do not start another server).
  if (did_any_scan) {
    return 0;
  }

  ts::HttpServer::Config cfg;
  cfg.port = cli.port;
  cfg.artifact_root = cli.artifact_root;
  cfg.index_title = "Typed Scanner Reports";
  cfg.scan_per_request = true;

  ts::HttpServer server(cfg);
  int rc = server.run();
  if (rc != 0) {
    std::cerr << "Server failed to start on port " << cfg.port << "\n";
    return rc;
  }
  return 0;
}
