#include "typed_scanner/run_json.hpp"
#include <sstream>
#include <cmath> // std::isfinite

namespace ts {

static void esc(std::ostringstream& o, const std::string& s){
  o << '"';
  for (char c : s){
    switch(c){
      case '\\': o << "\\\\"; break;
      case '"':  o << "\\\""; break;
      case '\n': o << "\\n";  break;
      case '\r': o << "\\r";  break;
      case '\t': o << "\\t";  break;
      default:   o << c;      break;
    }
  }
  o << '"';
}

static inline double safe_num(double v){ return std::isfinite(v) ? v : 0.0; }

std::string RunJsonWriter::to_json(const RunJsonPayload& p) {
  std::ostringstream o;
  o << "{";
  o << "\"rows\":" << p.rows << ",";
  o << "\"bytes\":" << p.bytes << ",";
  o << "\"wall_time_ms\":" << safe_num(p.wall_time_ms) << ",";
  o << "\"throughput_mb_s\":" << safe_num(p.throughput_mb_s) << ",";
  o << "\"tokens_per_sec\":" << safe_num(p.tokens_per_sec) << ",";
  o << "\"allocs_per_sec\":" << safe_num(p.allocs_per_sec) << ",";
  o << "\"p50_ms\":" << safe_num(p.p50_ms) << ",";
  o << "\"p95_ms\":" << safe_num(p.p95_ms) << ",";
  o << "\"peak_rss_mb\":" << safe_num(p.peak_rss_mb) << ",";
  o << "\"cpu_pct\":" << safe_num(p.cpu_pct) << ",";

  o << "\"stage_times\":[";
  for (size_t i=0;i<p.stage_times.size();++i){
    if (i) o << ",";
    o << "{\"stage\":"; esc(o, p.stage_times[i].first);
    o << ",\"duration_ms\":" << p.stage_times[i].second << "}";
  }
  o << "],";

  o << "\"errors_by_field\":{";
  bool first=true;
  for (auto& kv : p.errors_by_field) {
    if (!first) o << ",";
    first=false;
    esc(o, kv.first); o << ":" << kv.second;
  }
  o << "},";

  o << "\"series\":[";
  for (size_t i=0;i<p.series.size();++i){
    if (i) o << ",";
    const auto& s = p.series[i];
    o << "{"
      << "\"time_ms\":"       << safe_num(s.time_ms)       << ","
      << "\"mb_s\":"          << safe_num(s.mb_s)          << ","
      << "\"rss_mb\":"        << safe_num(s.rss_mb)        << ","
      << "\"allocs_per_sec\":"<< safe_num(s.allocs_per_sec)
      << "}";
  }
  o << "],";

  o << "\"filename\":";     esc(o, p.filename);     o << ",";
  o << "\"content_type\":"; esc(o, p.content_type); o << ",";
  o << "\"etag\":";         esc(o, p.etag);         o << ",";
  o << "\"file_size\":" << p.file_size;

  o << "}";
  return o.str();
}

}
