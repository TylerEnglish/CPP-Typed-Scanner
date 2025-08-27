#include "typed_scanner/date_parse.hpp"
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <string_view>

// NOTE: Portable, small subset ISO-8601 parser (YYYY-MM-DD[THH:MM:SS[.ms]][Z])
// Timezone offsets are treated as Z (UTC) for MVP; refine later if needed.

namespace ts {

static bool is_digit(char c){ return c>='0' && c<='9'; }

static bool parse_int(std::string_view s, int& out) {
  if (s.empty()) return false;
  int v = 0;
  for (char c : s) { if (!is_digit(c)) return false; v = v*10 + (c - '0'); }
  out = v; return true;
}

std::optional<std::int64_t> parse_iso8601_ms(std::string_view s) {
  // Expected forms:
  // YYYY-MM-DD
  // YYYY-MM-DDTHH:MM:SS
  // YYYY-MM-DDTHH:MM:SS.mmm
  // All optionally suffixed with 'Z' or an offset (ignored as Z here)

  if (s.size() < 10) return std::nullopt;
  int Y,M,D,h=0,m=0,sec=0,ms=0;

  if (!(parse_int(s.substr(0,4), Y) && s[4]=='-' && parse_int(s.substr(5,2), M) && s[7]=='-' && parse_int(s.substr(8,2), D)))
    return std::nullopt;

  size_t i = 10;
  if (i < s.size() && (s[i]=='T' || s[i]==' ')) {
    ++i;
    if (i+7 <= s.size()) {
      if (!(parse_int(s.substr(i,2), h) && s[i+2]==':' && parse_int(s.substr(i+3,2), m) && s[i+5]==':' && parse_int(s.substr(i+6,2), sec)))
        return std::nullopt;
      i += 8;
      if (i < s.size() && s[i]=='.') {
        size_t j=i+1, k=j;
        while (k < s.size() && is_digit(s[k]) && (k-j) < 3) ++k; // up to 3 digits
        int frac=0; if (!parse_int(s.substr(j,k-j), frac)) return std::nullopt;
        if ((k-j)==1) ms = frac*100;
        else if ((k-j)==2) ms = frac*10;
        else ms = frac;
        i = k;
      }
    } else {
      return std::nullopt;
    }
  }

  // Build a time_point in UTC ignoring offsets
  std::tm tm{}; tm.tm_year = Y - 1900; tm.tm_mon = M - 1; tm.tm_mday = D;
  tm.tm_hour = h; tm.tm_min = m; tm.tm_sec = sec;

  // timegm is nonstandard; approximate using time_t assuming system in UTC or TZ set.
#if defined(_WIN32)
  // Windows: _mkgmtime
  std::time_t t = _mkgmtime(&tm);
#else
  std::time_t t = timegm(&tm);
#endif
  if (t == (std::time_t)-1) return std::nullopt;

  using namespace std::chrono;
  auto ms_epoch = duration_cast<milliseconds>(system_clock::from_time_t(t).time_since_epoch()).count();
  return static_cast<std::int64_t>(ms_epoch + ms);
}

}
