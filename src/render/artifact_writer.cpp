#include "typed_scanner/artifact_writer.hpp"
#include "typed_scanner/mustache_renderer.hpp"
#include "typed_scanner/path_utils.hpp"
#include <filesystem>
#include <fstream>

namespace ts {

bool write_report_dir(const std::string& artifact_root,
                      const std::string& slug,
                      const std::string& run_json_str,
                      std::string* err_out) {
  const std::filesystem::path out_dir =
      std::filesystem::path(artifact_root) / slug;

  // (A) Save run.json alongside report.html (handy for debugging)
  {
    std::filesystem::create_directories(out_dir);
    std::ofstream rj(out_dir / "run.json", std::ios::binary);
    if (!rj) {
      if (err_out) *err_out = "failed to write run.json";
      return false;
    }
    rj.write(run_json_str.data(),
             static_cast<std::streamsize>(run_json_str.size()));
  }

  // (B) Render report.html + copy CSS/JS assets to the same folder
  ts::MustacheRenderer::Config rcfg;
#ifdef TS_DEFAULT_TEMPLATE_DIR
  rcfg.template_dir = TS_DEFAULT_TEMPLATE_DIR;
#else
  rcfg.template_dir = "templates";
#endif
  rcfg.partials_dir = rcfg.template_dir + "/partials";
  rcfg.static_js    = {"web/js/vega.min.js",
                       "web/js/vega-lite.min.js",
                       "web/js/vega-embed.min.js"};
  rcfg.static_css   = {"web/css/report.css"};

  ts::MustacheRenderer renderer(rcfg);
  const bool ok = renderer.render_to_dir("report.mustache",
                                         run_json_str,
                                         out_dir.string(),
                                         "report.html",
                                         /*copy_assets=*/true);
  if (!ok && err_out) *err_out = renderer.last_error();
  return ok;
}

}
