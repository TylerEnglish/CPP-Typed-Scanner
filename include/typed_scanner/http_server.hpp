#pragma once
#include <functional>
#include <string>
#include <string_view>

namespace ts {

// Tiny wrapper around cpp-httplib; serves `/` index + `/reports/<slug>/report.html`
class HttpServer {
public:
  struct Config {
    std::string artifact_root = "artifacts/typed-scanner";
    std::string index_title   = "Typed Scanner Reports";
    int port = 8080;
    bool scan_per_request = true; // allow re-scan of artifacts on GET /
  };

  explicit HttpServer(Config cfg);
  ~HttpServer();

  // Non-blocking start; returns false on bind error.
  bool start();

  // Blocking run (alternative); returns when server stops.
  int run();

  // Stop if running.
  void stop();

private:
  struct Impl;
  Impl* p_;
};

}
