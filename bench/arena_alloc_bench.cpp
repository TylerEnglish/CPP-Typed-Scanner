#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#if __has_include("typed_scanner/arena.hpp")
  #include "typed_scanner/arena.hpp"
  #define TS_HAVE_ARENA 1
#else
  #include <memory_resource>
  #define TS_HAVE_ARENA 0
#endif

using clk = std::chrono::steady_clock;

static std::vector<std::string> make_payloads(std::size_t n, std::size_t min_len=16, std::size_t max_len=96) {
  std::vector<std::string> v; v.reserve(n);
  std::mt19937 rng(42);
  // FIX: brace-init the distribution to avoid the most vexing parse
  std::uniform_int_distribution<int> len{static_cast<int>(min_len), static_cast<int>(max_len)};
  for (size_t i=0;i<n;++i) {
    std::string s(static_cast<std::size_t>(len(rng)), 'x');
    for (size_t j=0;j<s.size();++j) s[j] = char('a' + (i + j*13) % 26);
    v.emplace_back(std::move(s));
  }
  return v;
}

int main(int argc, char** argv){
  std::size_t n = 5'000'00;   // 500k payloads
  int iters = 5;
  std::size_t arena_sz = 8*1024*1024;

  for (int i=1;i<argc;++i){
    std::string s(argv[i]); auto eq=s.find('=');
    auto k=s.substr(0,eq), v=(eq==std::string::npos)?"":s.substr(eq+1);
    if (k=="--n") n = std::stoull(v);
    else if (k=="--iters") iters = std::stoi(v);
    else if (k=="--arena") arena_sz = std::stoull(v);
    else if (k=="--help"||k=="-h"){
      std::cout << "Usage: ts_bench_arena_alloc [--n=500000] [--iters=5] [--arena=8388608]\n"; return 0;
    }
  }

  auto payloads = make_payloads(n);

#if TS_HAVE_ARENA
  std::cout << "[arena] using ts::Arena, size=" << arena_sz << "\n";
  for (int k=1;k<=iters;++k) {
    ts::Arena a(arena_sz);
    std::size_t bytes=0;
    auto t0 = clk::now();
    for (const auto& s: payloads) {
      // If your Arena API differs, adjust this one line:
      char* p = static_cast<char*>(a.alloc(s.size()));
      std::memcpy(p, s.data(), s.size());
      bytes += s.size();
      if (bytes > arena_sz*3/2) { a.reset(); bytes = 0; }
    }
    auto t1 = clk::now();
    double sec = std::chrono::duration<double>(t1-t0).count();
    std::cout << "  iter " << k << ": items=" << payloads.size()
              << " time=" << sec << "s  rate=" << (payloads.size()/sec)/1e6 << " M items/s\n";
  }
#else
  std::cout << "[arena] ts::Arena not available; using std::pmr::monotonic_buffer_resource fallback\n";
  std::vector<char> buf(arena_sz);
  for (int k=1;k<=iters;++k) {
    std::pmr::monotonic_buffer_resource mbr(buf.data(), buf.size());
    std::pmr::vector<std::pmr::string> bucket{&mbr};
    auto t0 = clk::now();
    for (const auto& s: payloads) bucket.emplace_back(s.data(), &mbr);
    auto t1 = clk::now();
    double sec = std::chrono::duration<double>(t1-t0).count();
    std::cout << "  iter " << k << ": items=" << bucket.size()
              << " time=" << sec << "s  rate=" << (bucket.size()/sec)/1e6 << " M items/s\n";
  }
#endif
  return 0;
}
