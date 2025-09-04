#include <cstdlib>
#include <vector>
#include <chrono>
#include <iostream>

int main(int argc, char** argv){
  size_t N = 500'000, iters = 50, sz = 64;
  if (argc>1) N=std::stoull(argv[1]);
  if (argc>2) iters=std::stoull(argv[2]);
  if (argc>3) sz=std::stoull(argv[3]);

  for(size_t it=1; it<=iters; ++it){
    auto t0 = std::chrono::high_resolution_clock::now();
    std::vector<void*> ptrs; ptrs.reserve(N);
    for(size_t i=0;i<N;i++){ ptrs.push_back(std::malloc(sz)); }
    for(void* p: ptrs) std::free(p);
    auto t1 = std::chrono::high_resolution_clock::now();
    double s = std::chrono::duration<double>(t1-t0).count();
    std::cout << "[malloc/free] iter " << it
              << ": items=" << N << " time=" << s
              << "s  rate=" << (N/s)/1e6 << " M items/s\n";
  }
}
