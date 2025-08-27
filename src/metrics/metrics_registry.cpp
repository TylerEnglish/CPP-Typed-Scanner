#include "typed_scanner/metrics.hpp"
#include <algorithm>
#include <chrono>

namespace ts {

void MetricsRegistry::reset() {
  rows_ = bytes_ = 0;
  cpu_pct_ = peak_rss_mb_ = 0.0;
  field_errs_.clear();
  stage_accum_ms_.clear();
  stage_starts_.clear();
}

void MetricsRegistry::start_stage(std::string_view name) {
  stage_starts_[std::string(name)] = std::chrono::steady_clock::now();
}

void MetricsRegistry::end_stage(std::string_view name) {
  auto key = std::string(name);
  auto it = stage_starts_.find(key);
  if (it == stage_starts_.end()) return;
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - it->second).count();
  stage_accum_ms_[key] += static_cast<std::uint64_t>(ms);
  stage_starts_.erase(it);
}

void MetricsRegistry::add_field_error(std::string_view field) {
  ++field_errs_[std::string(field)];
}

RunStats MetricsRegistry::snapshot(double wall_ms, double tokens_per_sec,
                                   double allocs_per_sec, double p50_ms, double p95_ms) const {
  RunStats r;
  r.rows = rows_;
  r.bytes = bytes_;
  r.tokens_per_sec = tokens_per_sec;
  r.allocs_per_sec = allocs_per_sec;
  r.p50_ms = p50_ms;
  r.p95_ms = p95_ms;
  r.cpu_pct = cpu_pct_;
  r.peak_rss_mb = peak_rss_mb_;
  r.throughput_mb_s = (wall_ms > 0.0) ? (bytes_ / (1024.0*1024.0)) / (wall_ms / 1000.0) : 0.0;

  r.errors_by_field = field_errs_;
  r.stages.reserve(stage_accum_ms_.size());
  for (auto& kv : stage_accum_ms_) r.stages.push_back(StageTiming{kv.first, kv.second});
  return r;
}

}
