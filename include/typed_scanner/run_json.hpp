#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ts {

struct RunJsonSeriesPoint {
  double time_ms = 0.0;
  double mb_s = 0.0;
  double rss_mb = 0.0;
  double allocs_per_sec = 0.0;
};

struct RunJsonPayload {
  // Top-level KPIs
  std::uint64_t rows = 0;
  std::uint64_t bytes = 0;
  double wall_time_ms = 0.0;
  double throughput_mb_s = 0.0;
  double tokens_per_sec = 0.0;
  double allocs_per_sec = 0.0;
  double p50_ms = 0.0;
  double p95_ms = 0.0;
  double peak_rss_mb = 0.0;
  double cpu_pct = 0.0;

  // Stages and errors
  std::vector<std::pair<std::string, std::uint64_t>> stage_times;
  std::unordered_map<std::string, std::uint64_t> errors_by_field;

  // Series timeline
  std::vector<RunJsonSeriesPoint> series;

  // Input metadata
  std::string filename;
  std::string content_type;
  std::string etag;
  std::uint64_t file_size = 0;
};

class RunJsonWriter {
public:
  // Serialize payload to pretty JSON string.
  static std::string to_json(const RunJsonPayload& p);
};

}
