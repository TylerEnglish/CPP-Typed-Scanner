#include <charconv>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <iostream>

int main(int argc, char** argv){
  size_t N = 1'000'000, iters = 50;
  if (argc > 1) N = std::stoull(argv[1]);
  if (argc > 2) iters = std::stoull(argv[2]);

  std::mt19937_64 rng(123);
  std::uniform_int_distribution<int> dist(0, 99'999'999);

  std::vector<std::string> strs; strs.reserve(N);
  for(size_t i=0;i<N;i++){ strs.push_back(std::to_string(dist(rng))); }

  std::vector<long long> out(N);
  for(size_t it=1; it<=iters; ++it){
    auto t0 = std::chrono::high_resolution_clock::now();
    size_t ok=0;
    for(size_t i=0;i<N;i++){
      long long v;
      auto p = std::from_chars(strs[i].data(), strs[i].data()+strs[i].size(), v, 10);
      ok += (p.ec == std::errc{}); out[i]=v;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double s = std::chrono::duration<double>(t1-t0).count();
    std::cout << "[from_chars] iter " << it << ": ok=" << ok
              << " time=" << s << "s  rate=" << (N/s)/1e6 << " M/s\n";
  }
}
