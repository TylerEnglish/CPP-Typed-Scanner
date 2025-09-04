#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <csignal>
#include "httplib.h"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

static std::string env_or(const char* k, const char* defv) {
  const char* v = std::getenv(k);
  return (v && *v) ? std::string(v) : std::string(defv);
}

static int read_pid(const fs::path& p) {
  std::ifstream in(p); int pid=0; in >> pid; return pid;
}

int main() {
  std::string bin = env_or("TS_SCANNER_BIN", "/opt/typed-scanner/bin/typed-scanner");
  int port = 18080;
  fs::path art = "/work/it-artifacts/http-" + std::to_string(std::time(nullptr));
  fs::create_directories(art);
  fs::path pidf = "/work/it-artifacts/http.pid";

  // Launch server in background (bash spawns, writes PID to pidf)
  std::string cmd = "bash -lc '\""+bin+"\" "
                    "--config=/work/configs/config.toml "
                    "--artifact-root=\""+art.string()+"\" "
                    "--port="+std::to_string(port)+" "
                    "> /work/it-artifacts/http.log 2>&1 & echo $! > "+pidf.string()+"'";
  int rc = std::system(cmd.c_str());
  if (rc != 0) { std::cerr << "[FAIL] could not start server\n"; return 1; }

  // Poll for readiness
  httplib::Client cli("127.0.0.1", port);
  bool up = false;
  for (int i=0;i<50;i++) {
    if (auto res = cli.Get("/")) {
      if (res->status == 200 && !res->body.empty()) { up = true; break; }
    }
    std::this_thread::sleep_for(200ms);
  }

  // Stop server
  int pid = read_pid(pidf);
  if (pid > 1) kill(pid, SIGTERM);

  if (!up) { std::cerr << "[FAIL] server did not respond with 200 on /\n"; return 1; }
  std::cout << "[PASS] http server GET / responded 200 with body\n";
  return 0;
}
