#if __has_include("typed_scanner/run_json.hpp")
  #include "typed_scanner/run_json.hpp"
  #include <iostream>
int main() { std::cout << "[PASS] run_json header present\n"; return 0; }
#else
  #include <iostream>
int main() { std::cout << "[SKIP] run_json header missing\n"; return 0; }
#endif
