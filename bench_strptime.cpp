#define _XOPEN_SOURCE
#include <ctime>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <iostream>

int main(int argc, char** argv){
  size_t N = 1'000'000, iters = 50;
  if (argc > 1) N = std::stoull(argv[1]);
  if (argc > 2) iters = std::stoull(argv[2]);

  std::mt19937 rng(123);
  std::uniform_int_distribution<int> y(1970, 2099), m(1,12), d(1,28);

  auto two = [](int x){ char b[3]; snprintf(b, sizeof b, "%02d", x); return std::string(b); };

  std::vector<std::string> strs; strs.reserve(N);
  for(size_t i=0;i<N;i++){
    int Y=y(rng), M=m(rng), D=d(rng);
    strs.push_back(std::to_string(Y)+"-"+two(M)+"-"+two(D));
  }

  for(size_t it=1; it<=iters; ++it){
    auto t0 = std::chrono::high_resolution_clock::now();
    size_t ok=0; std::tm tm{};
    for(size_t i=0;i<N;i++){ tm = {};
      ok += (strptime(strs[i].c_str(), "%Y-%m-%d", &tm) != nullptr);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double s = std::chrono::duration<double>(t1-t0).count();
    std::cout << "[strptime] iter " << it << ": ok=" << ok
              << " time=" << s << "s  rate=" << (N/s)/1e6 << " M/s\n";
  }
}
