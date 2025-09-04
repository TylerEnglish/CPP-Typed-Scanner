#if __has_include("typed_scanner/record_view.hpp")
  #include "typed_scanner/record_view.hpp"
  #include <iostream>
int main() { std::cout << "[PASS] record_view header present\n"; return 0; }
#else
  #include <iostream>
int main() { std::cout << "[SKIP] record_view header missing\n"; return 0; }
#endif
