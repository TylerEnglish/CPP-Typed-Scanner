#include <filesystem>
#include <iostream>
#include <string>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <vector>
#include <simdjson.h>

namespace fs = std::filesystem;

static std::string env_or(const char* k, const char* defv) {
  const char* v = std::getenv(k);
  return (v && *v) ? std::string(v) : std::string(defv);
}

static bool exists(const fs::path& p) { return fs::exists(p); }

static fs::path choose_fixture() {
  // Prefer runtime copy in /work, fallback to installed share
  fs::path p1("/work/tests/data/utf8.csv");
  if (exists(p1)) return p1;
  fs::path p2("/opt/typed-scanner/share/typed-scanner/tests/data/utf8.csv");
  return p2;
}

static fs::path newest_runjson_under(const fs::path& root, fs::path* out_dir = nullptr) {
  fs::file_time_type best_ts{};
  fs::path best;
  for (auto& it : fs::recursive_directory_iterator(root)) {
    if (!it.is_regular_file()) continue;
    if (it.path().filename() == "run.json") {
      auto ts = fs::last_write_time(it.path());
      if (best.empty() || ts > best_ts) { best_ts = ts; best = it.path(); }
    }
  }
  if (!best.empty() && out_dir) *out_dir = best.parent_path();
  return best;
}

int main() {
  std::string bin = env_or("TS_SCANNER_BIN", "/opt/typed-scanner/bin/typed-scanner");
  fs::path in = choose_fixture();
  if (!exists(in)) { std::cerr << "[ERR] fixture not found: " << in << "\n"; return 2; }

  // Unique artifact root for this test
  fs::path art = "/work/it-artifacts/csv-" + std::to_string(std::time(nullptr));
  fs::create_directories(art);

  // Run scanner once
  std::string cmd = "bash -lc '\""+bin+"\" "
                    "--config=/work/configs/config.toml "
                    "--artifact-root=\""+art.string()+"\" "
                    "--slug-mode=basename --slug-len=64 "
                    "--scan \""+in.string()+"\" >/work/it-artifacts/csv.log 2>&1'";
  int rc = std::system(cmd.c_str());
  if (rc != 0) {
    std::cerr << "[FAIL] scanner returned " << rc << " (see /work/it-artifacts/csv.log)\n";
    return 1;
  }

  // Find newest run.json
  fs::path outdir;
  fs::path runjson = newest_runjson_under(art, &outdir);
  if (runjson.empty()) { std::cerr << "[FAIL] no run.json produced under " << art << "\n"; return 1; }

  // Basic assertions on run.json
  simdjson::ondemand::parser p;
  auto json = simdjson::padded_string::load(runjson.string());
  auto doc = p.iterate(json);

  uint64_t rows = doc["rows"].get_uint64().value_or(0);
  double mbps   = double(doc["throughput_mb_s"].get_double().value_or(0.0));
  std::string_view ctype = doc["content_type"].get_string().value_or("");
  std::string_view fname = doc["filename"].get_string().value_or("");

  bool ok = true;
  if (rows == 0)  { std::cerr << "[FAIL] rows==0 in run.json\n"; ok = false; }
  if (mbps < 0)   { std::cerr << "[FAIL] throughput_mb_s negative\n"; ok = false; }
  if (ctype.find("csv") == std::string_view::npos) {
    std::cerr << "[FAIL] content_type not CSV-like: " << ctype << "\n"; ok = false;
  }
  if (fname.find("utf8.csv") == std::string_view::npos) {
    std::cerr << "[WARN] filename does not include utf8.csv: " << fname << "\n";
  }

  if (!ok) return 1;
  std::cout << "[PASS] end-to-end CSV: rows=" << rows
            << " mb/s=" << mbps
            << " out=" << outdir << "\n";
  return 0;
}
