#pragma once
#include <cstdint>
#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ts {

struct StageTiming {
  std::string name;
  std::uint64_t duration_ms = 0;
};

struct RunStats {
  std::uint64_t rows = 0;
  std::uint64_t bytes = 0;
  double throughput_mb_s = 0.0;
  double tokens_per_sec = 0.0;
  double allocs_per_sec = 0.0;
  double p50_ms = 0.0;
  double p95_ms = 0.0;
  double cpu_pct = 0.0;
  double peak_rss_mb = 0.0;

  std::vector<StageTiming> stages;
  std::unordered_map<std::string, std::uint64_t> errors_by_field;
};

class MetricsRegistry {
public:
  void reset();
  void add_row() noexcept { ++rows_; }
  void add_bytes(std::uint64_t b) noexcept { bytes_ += b; }
  void set_cpu_pct(double v) noexcept { cpu_pct_ = v; }
  void set_peak_rss_mb(double v) noexcept { peak_rss_mb_ = v; }

  void start_stage(std::string_view name);
  void end_stage(std::string_view name);

  void add_field_error(std::string_view field);
  RunStats snapshot(double wall_ms, double tokens_per_sec,
                    double allocs_per_sec, double p50_ms, double p95_ms) const;

private:
  std::uint64_t rows_{0};
  std::uint64_t bytes_{0};
  double cpu_pct_{0.0};
  double peak_rss_mb_{0.0};
  std::unordered_map<std::string, std::uint64_t> field_errs_;
  std::unordered_map<std::string, std::uint64_t> stage_accum_ms_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> stage_starts_;
};

}
