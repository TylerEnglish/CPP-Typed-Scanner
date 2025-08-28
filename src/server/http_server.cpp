#include "typed_scanner/http_server.hpp"
#include "typed_scanner/path_utils.hpp"
#include <httplib.h>
#include <filesystem>
#include <vector>
#include <string>
#include <fstream>     // <-- add
#include <sstream>     // <-- add

namespace ts {

static std::string guess_mime(const std::string& name) {
  auto ends = [&](const char* s){
    const size_t n = std::strlen(s), m = name.size();
    return m >= n && std::equal(s, s+n, name.c_str() + (m - n),
                                [](char a, char b){ return std::tolower(a)==std::tolower(b); });
  };
  if (ends(".html")) return "text/html; charset=utf-8";
  if (ends(".css"))  return "text/css; charset=utf-8";
  if (ends(".js"))   return "application/javascript; charset=utf-8";
  if (ends(".json")) return "application/json; charset=utf-8";
  if (ends(".txt"))  return "text/plain; charset=utf-8";
  return "application/octet-stream";
}

struct HttpServer::Impl {
  Config cfg;
  httplib::Server svr;

  explicit Impl(Config c) : cfg(std::move(c)) {}

  std::vector<std::string> slugs() const {
    std::vector<std::string> out;
    std::filesystem::path root(cfg.artifact_root);
    if (!std::filesystem::exists(root)) return out;
    for (auto& d : std::filesystem::directory_iterator(root)) {
      if (d.is_directory()) out.push_back(d.path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
  }

  std::string index_html() const {
    auto items = slugs();
    std::string html = "<!doctype html><html><head><meta charset='utf-8'><title>";
    html += cfg.index_title + "</title></head><body><h1>" + cfg.index_title + "</h1><ul>";
    for (auto& s : items) {
      html += "<li><a href=\"/reports/" + s + "/report.html\">" + s + "</a></li>";
    }
    html += "</ul></body></html>";
    return html;
  }

  // Serve a file under artifact_root/<slug>/<rel>, preventing traversal.
  bool serve_under_slug(const std::string& slug,
                        const std::string& rel,
                        httplib::Response& res) const {
    if (rel.find("..") != std::string::npos) { res.status = 400; return true; }

    std::filesystem::path base = std::filesystem::path(cfg.artifact_root) / slug;
    std::error_code ec;
    auto base_canon = std::filesystem::weakly_canonical(base, ec);
    if (ec) { res.status = 404; return true; }

    auto target = base_canon / rel;
    auto target_canon = std::filesystem::weakly_canonical(target, ec);
    if (ec) { res.status = 404; return true; }

    // Ensure target is inside base
    auto mismatch = std::mismatch(base_canon.begin(), base_canon.end(), target_canon.begin(), target_canon.end());
    if (mismatch.first != base_canon.end()) { res.status = 403; return true; }

    std::ifstream in(target_canon, std::ios::binary);
    if (!in) { res.status = 404; return true; }

    std::ostringstream ss; ss << in.rdbuf();
    res.set_content(ss.str(), guess_mime(target_canon.filename().string()).c_str());
    return true;
  }

  void routes() {
    // Index
    svr.Get("/", [this](const httplib::Request&, httplib::Response& res) {
      res.set_content(index_html(), "text/html; charset=utf-8");
    });

    // report.html (kept, but could be covered by the generic handler below)
    svr.Get(R"(/reports/([^/]+)/report\.html)", [this](const httplib::Request& req, httplib::Response& res) {
      auto slug = req.matches[1].str();
      (void)serve_under_slug(slug, "report.html", res);
    });

    // Any other file under a slug (CSS/JS/JSON etc.)
    svr.Get(R"(/reports/([^/]+)/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
      auto slug = req.matches[1].str();
      auto rel  = req.matches[2].str(); // e.g., "report.css", "vega.min.js", "run.json"
      (void)serve_under_slug(slug, rel, res);
    });
  }
};

HttpServer::HttpServer(Config cfg) : p_(new Impl(std::move(cfg))) { p_->routes(); }
HttpServer::~HttpServer() { delete p_; }

bool HttpServer::start() {
  return p_->svr.bind_to_port("0.0.0.0", p_->cfg.port);
}

int HttpServer::run() {
  if (!start()) return -1;
  p_->svr.listen_after_bind();
  return 0;
}
void HttpServer::stop() { p_->svr.stop(); }

}
