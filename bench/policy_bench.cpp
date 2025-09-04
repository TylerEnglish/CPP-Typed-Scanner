#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <cstring>

#include "fast_float/fast_float.h"

using clk = std::chrono::steady_clock;

static std::vector<std::string> make_numbers(size_t n) {
  std::vector<std::string> v; v.reserve(n);
  std::mt19937_64 rng(12345);
  std::uniform_real_distribution<double> d(-1e6, 1e6);
  for (size_t i=0;i<n;++i) {
    double x = d(rng);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6f", x);
    v.emplace_back(buf);
  }
  return v;
}

struct DateParts { int y,m,d,hh,mm,ss; bool ok; };
static inline int parse_2(const char* p){ return (p[0]-'0')*10 + (p[1]-'0'); }
static inline int parse_4(const char* p){ return (p[0]-'0')*1000 + (p[1]-'0')*100 + (p[2]-'0')*10 + (p[3]-'0'); }

// Minimal ISO8601 parser: "YYYY-MM-DD" or "YYYY-MM-DDTHH:MM:SSZ"
static DateParts parse_iso8601(std::string_view s){
  DateParts r{0,0,0,0,0,0,false};
  if (s.size()<10) return r;
  if (s[4]!='-' || s[7]!='-') return r;
  r.y = parse_4(s.data());
  r.m = parse_2(s.data()+5);
  r.d = parse_2(s.data()+8);
  if (s.size() >= 19 && (s[10]=='T' || s[10]==' ')) {
    r.hh = parse_2(s.data()+11);
    r.mm = parse_2(s.data()+14);
    r.ss = parse_2(s.data()+17);
  }
  r.ok = (1<=r.m && r.m<=12 && 1<=r.d && r.d<=31);
  return r;
}

static std::vector<std::string> make_dates(size_t n) {
  std::vector<std::string> v; v.reserve(n);
  for (size_t i=0;i<n;++i) {
    int y = 2000 + int(i%25);
    int m = int(i%12)+1;
    int d = int(i%28)+1;
    int H = int((i*7)%24), M=int((i*11)%60), S=int((i*13)%60);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", y,m,d,H,M,S);
    v.emplace_back(buf);
  }
  return v;
}

static void bench_numbers(size_t n, int iters) {
  auto data = make_numbers(n);
  std::cout << "\n[numeric] samples=" << n << " iters=" << iters << "\n";
  for (int k=1;k<=iters;++k) {
    std::size_t ok=0;
    auto t0 = clk::now();
    for (auto& s: data) {
      double out;
      auto [ptr, ec] = fast_float::from_chars(s.data(), s.data()+s.size(), out);
      if (ec == std::errc()) ++ok;
    }
    auto t1 = clk::now();
    double sec = std::chrono::duration<double>(t1-t0).count();
    std::cout << "  iter " << k << ": ok=" << ok
              << " time=" << sec << "s  rate=" << (n/sec)/1e6 << " M/s\n";
  }
}

static void bench_dates(size_t n, int iters) {
  auto data = make_dates(n);
  std::cout << "\n[dates] samples=" << n << " iters=" << iters << "\n";
  for (int k=1;k<=iters;++k) {
    std::size_t ok=0;
    auto t0 = clk::now();
    for (auto& s: data) {
      auto r = parse_iso8601(s);
      if (r.ok) ++ok;
    }
    auto t1 = clk::now();
    double sec = std::chrono::duration<double>(t1-t0).count();
    std::cout << "  iter " << k << ": ok=" << ok
              << " time=" << sec << "s  rate=" << (n/sec)/1e6 << " M/s\n";
  }
}

int main(int argc, char** argv){
  size_t n = 1'000'000;
  int iters = 3;
  for (int i=1;i<argc;++i){
    std::string s(argv[i]);
    auto eq = s.find('=');
    auto k = s.substr(0,eq); auto v = (eq==std::string::npos)?"":s.substr(eq+1);
    if (k=="--n") n = std::stoull(v);
    else if (k=="--iters") iters = std::stoi(v);
    else if (k=="--help"||k=="-h"){
      std::cout << "Usage: ts_bench_policy [--n=1000000] [--iters=3]\n";
      return 0;
    }
  }
  bench_numbers(n, iters);
  bench_dates(n, iters);
  return 0;
}
