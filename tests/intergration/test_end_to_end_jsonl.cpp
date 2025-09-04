#include <filesystem>
#include <iostream>
#include <string>
#include <cstdlib>
#include <simdjson.h>

namespace fs = std::filesystem;

static std::string env_or(const char* k, const char* defv) {
  const char* v = std::getenv(k);
  return (v && *v) ? std::string(v) : std::string(defv);
}
static bool exists(const fs::path& p) { return fs::exists(p); }

static fs::path fixture() {
  fs::path p1("/work/tests/data/bad.jsonl");
  if (exists(p1)) return p1;
  return fs::path("/opt/typed-scanner/share/typed-scanner/tests/data/bad.jsonl");
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
  fs::path in = fixture();
  if (!exists(in)) { std::cerr << "[ERR] fixture not found: " << in << "\n"; return 2; }

  fs::path art = "/work/it-artifacts/jsonl-" + std::to_string(std::time(nullptr));
  fs::create_directories(art);

  std::string cmd = "bash -lc '\""+bin+"\" "
                    "--config=/work/configs/config.toml "
                    "--artifact-root=\""+art.string()+"\" "
                    "--slug-mode=basename --slug-len=64 "
                    "--scan \""+in.string()+"\" >/work/it-artifacts/jsonl.log 2>&1'";
  int rc = std::system(cmd.c_str());
  if (rc != 0) {
    std::cerr << "[FAIL] scanner returned " << rc << " (see /work/it-artifacts/jsonl.log)\n";
    return 1;
  }

  fs::path outdir;
  fs::path runjson = newest_runjson_under(art, &outdir);
  if (runjson.empty()) { std::cerr << "[FAIL] no run.json produced\n"; return 1; }

  simdjson::ondemand::parser p;
  auto json = simdjson::padded_string::load(runjson.string());
  auto doc = p.iterate(json);

  uint64_t rows = doc["rows"].get_uint64().value_or(0);
  std::string_view ctype = doc["content_type"].get_string().value_or("");
  auto errs = doc["errors_by_field"];
  bool has_errs = false;
  if (errs.type() == simdjson::ondemand::json_type::object) {
    for (auto kv : errs.get_object()) { (void)kv; has_errs = true; break; }
  }

  // "bad.jsonl" may produce errors; E2E is "pass" if report exists and
  // either some rows or some errors were recorded, and the type is json-ish
  bool ok = true;
  if (ctype.find("json") == std::string_view::npos) {
    std::cerr << "[FAIL] content_type not json-ish: " << ctype << "\n"; ok = false;
  }
  if (!has_errs && rows == 0) {
    std::cerr << "[FAIL] neither rows nor errors reflected in run.json\n"; ok = false;
  }

  if (!ok) return 1;
  std::cout << "[PASS] end-to-end JSONL: rows=" << rows
            << " errors_present=" << (has_errs?"true":"false")
            << " out=" << outdir << "\n";
  return 0;
}
