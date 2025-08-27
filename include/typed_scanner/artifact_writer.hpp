#pragma once
#include <string>

namespace ts {

// Build run.json (outside) and hand it to this helper.
// It writes:
//   artifacts/typed-scanner/<slug>/report.html
//   + co-located CSS/JS (vega, vega-lite, vega-embed)
//   + run.json (for debugging/inspection)
bool write_report_dir(const std::string& artifact_root,
                      const std::string& slug,
                      const std::string& run_json_str,
                      std::string* err_out = nullptr);

} 
