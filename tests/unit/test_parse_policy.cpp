#if __has_include("typed_scanner/parse_policy.hpp")
  #include "typed_scanner/parse_policy.hpp"
  #include <iostream>
int main() { std::cout << "[PASS] parse_policy header present\n"; return 0; }
#else
  #include <iostream>
int main() { std::cout << "[SKIP] parse_policy header missing\n"; return 0; }
#endif
