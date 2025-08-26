#include <cstdlib>
#include <iostream>
#include <httplib.h>

static int read_port() {
  if (const char* p = std::getenv("TS_PORT")) {
    int v = std::atoi(p);
    return (v > 0 && v < 65536) ? v : 8080;
  }
  return 8080;
}

int main() {
  int port = read_port();
  httplib::Server svr;

  // Very simple index so healthcheck "/" works too
  svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
    res.set_content("typed-scanner: OK\n", "text/plain");
  });

  // Explicit health endpoint
  svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
    res.set_content("ok", "text/plain");
  });

  std::cout << "[typed-scanner] listening on 0.0.0.0:" << port << std::endl;
  if (!svr.listen("0.0.0.0", port)) {
    std::cerr << "[typed-scanner] failed to bind\n";
    return 2;
  }
  return 0;
}
