#include "typed_scanner/http_server.hpp"
#include "typed_scanner/path_utils.hpp"
#include <httplib.h>
#include <filesystem>
#include <vector>
#include <string>

namespace ts {

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

  void routes() {
    svr.Get("/", [this](const httplib::Request&, httplib::Response& res) {
      res.set_content(index_html(), "text/html; charset=utf-8");
    });

    svr.Get(R"(/reports/(.+)/report\.html)", [this](const httplib::Request& req, httplib::Response& res) {
      auto slug = req.matches[1].str();
      auto p = std::filesystem::path(cfg.artifact_root) / slug / "report.html";
      if (!std::filesystem::exists(p)) { res.status = 404; res.set_content("not found", "text/plain"); return; }
      std::ifstream in(p, std::ios::binary);
      std::ostringstream ss; ss << in.rdbuf();
      res.set_content(ss.str(), "text/html; charset=utf-8");
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
