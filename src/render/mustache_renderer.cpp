#include "typed_scanner/mustache_renderer.hpp"
#include "typed_scanner/path_utils.hpp"

#if __has_include(<kainjow/mustache.hpp>)
  #include <kainjow/mustache.hpp>
#elif __has_include(<mustache.hpp>)
  #include <mustache.hpp>
#else
  #error "kainjow/Mustache header not found"
#endif

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <cctype>

namespace ts {

MustacheRenderer::MustacheRenderer() : cfg_{} {}

static std::string read_file(const std::string& path, std::string& err) {
  std::ifstream in(path, std::ios::binary);
  if (!in) { err += "open failed: " + path + "\n"; return {}; }
  std::ostringstream ss; ss << in.rdbuf();
  return ss.str();
}

// naive "{{> name}}" inliner that looks for partial files in cfg_.partials_dir
static std::string inline_partials(std::string tpl,
                                   const std::filesystem::path& partials_dir,
                                   std::string& err) {
  size_t pos = 0;
  while ((pos = tpl.find("{{>", pos)) != std::string::npos) {
    size_t name_start = pos + 3;
    while (name_start < tpl.size() && std::isspace(static_cast<unsigned char>(tpl[name_start])))
      ++name_start;
    size_t close = tpl.find("}}", name_start);
    if (close == std::string::npos) break;

    // trim spaces before the closing braces
    size_t name_end = close;
    while (name_end > name_start &&
           std::isspace(static_cast<unsigned char>(tpl[name_end - 1])))
      --name_end;

    std::string partial_name = tpl.substr(name_start, name_end - name_start);
    if (partial_name.empty()) { pos = close + 2; continue; }

    // read partial file (allow both "<name>" and "<name>.mustache")
    std::string perr;
    std::filesystem::path p1 = partials_dir / partial_name;
    std::filesystem::path p2 = partials_dir / (partial_name + ".mustache");

    std::string content = read_file(p1.string(), perr);
    if (content.empty()) content = read_file(p2.string(), perr);
    if (content.empty()) {
      err += "partial not found: " + p1.string() + " | " + p2.string() + "\n";
      // leave the tag as-is and move on
      pos = close + 2;
      continue;
    }

    // replace the whole tag with the partial contents
    tpl.replace(pos, (close + 2) - pos, content);
    // continue after inserted content
    pos += content.size();
  }
  return tpl;
}

// --- copy one asset if it exists (tries a couple locations) ---------------
static bool copy_one_asset(const std::filesystem::path& src_hint,
                           const std::filesystem::path& out_dir,
                           std::string& err) {
  std::error_code ec;
  auto exists_ok = [&](const std::filesystem::path& p) -> bool {
    return std::filesystem::exists(p);
  };

  std::vector<std::filesystem::path> candidates;
  candidates.emplace_back(src_hint); // as-is

#ifdef TS_DEFAULT_STATIC_DIR
  {
    std::filesystem::path base(TS_DEFAULT_STATIC_DIR);
    candidates.emplace_back(base / src_hint.filename()); // flatten
    candidates.emplace_back(base / src_hint);            // preserve subdirs
  }
#endif

  std::filesystem::path src{};
  for (auto& c : candidates) {
    if (exists_ok(c)) { src = c; break; }
  }

  if (src.empty()) {
    err += "asset not found: " + src_hint.string() + "\n";
    return false; // non-fatal
  }

  if (!ensure_parent_dirs(out_dir)) {
    err += "cannot ensure out_dir: " + out_dir.string() + "\n";
    return false;
  }

  auto dst = out_dir / src.filename();
  std::filesystem::copy_file(src, dst,
      std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    err += "copy failed: " + src.string() + " -> " + dst.string() + " (" + ec.message() + ")\n";
    return false;
  }
  return true;
}

MustacheRenderer::MustacheRenderer(Config cfg) : cfg_(std::move(cfg)) {}

bool MustacheRenderer::render_to_file(std::string_view template_name,
                                      std::string_view context_json,
                                      std::string_view out_path) {
  err_.clear();

  const auto tpl_path =
      (std::filesystem::path(cfg_.template_dir) / std::string(template_name)).string();

  std::string tpl = read_file(tpl_path, err_);
  if (tpl.empty() && !err_.empty()) return false;

  // expand partials before handing to the engine
  tpl = inline_partials(std::move(tpl), std::filesystem::path(cfg_.partials_dir), err_);

  kainjow::mustache::mustache view(tpl);
  if (!view.is_valid()) { err_ = view.error_message(); return false; }

  kainjow::mustache::data data;
  // pass raw JSON; template should use triple-stache {{{ctx}}} to avoid escaping
  data.set("ctx", std::string(context_json));

  // keep escaping neutral; JSON should be included with {{{ctx}}}
  view.set_custom_escape([](const std::string& s){ return s; });

  const std::string rendered = view.render(data);
  if (rendered.empty() && !view.is_valid()) { err_ = view.error_message(); return false; }

  if (!ensure_parent_dirs(std::filesystem::path(out_path))) { err_ = "mkdir -p failed"; return false; }

  std::ofstream out(std::string(out_path), std::ios::binary);
  if (!out) { err_ = "write failed: " + std::string(out_path); return false; }
  out.write(rendered.data(), static_cast<std::streamsize>(rendered.size()));
  return true;
}

bool MustacheRenderer::render_to_dir(std::string_view template_name,
                                     std::string_view context_json,
                                     std::string_view out_dir,
                                     std::string_view out_name,
                                     bool copy_assets) {
  err_.clear();
  const std::filesystem::path outdir{std::string(out_dir)};
  const std::filesystem::path outpath = outdir / std::string(out_name);

  if (!render_to_file(template_name, context_json, outpath.string())) {
    return false; // err_ set
  }
  if (!copy_assets) return true;

  std::vector<std::string> js = cfg_.static_js.empty()
      ? std::vector<std::string>{"web/js/vega.min.js","web/js/vega-lite.min.js","web/js/vega-embed.min.js"}
      : cfg_.static_js;

  std::vector<std::string> css = cfg_.static_css.empty()
      ? std::vector<std::string>{"web/css/report.css"}
      : cfg_.static_css;

  for (auto& s : js)  copy_one_asset(std::filesystem::path(s), outdir, err_);
  for (auto& s : css) copy_one_asset(std::filesystem::path(s), outdir, err_);

  return true; // non-fatal if some assets are missing
}

}
