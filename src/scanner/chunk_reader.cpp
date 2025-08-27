#include "typed_scanner/chunk_reader.hpp"
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

namespace ts {

struct ChunkReader::Impl {
  std::string path;
  Config cfg;
  int last_errno{0};
  std::uint64_t bytes{0};

  bool for_each_line(const LineCallback& cb) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { last_errno = errno; return false; }

    std::vector<char> buf(cfg.chunk_bytes + 1, 0);
    std::string carry;
    carry.reserve(256);
    bool skipping_oversize = false; // if true, drop until next newline

    while (true) {
      std::size_t n = std::fread(buf.data(), 1, cfg.chunk_bytes, f);
      if (n == 0 && std::ferror(f)) { last_errno = errno; std::fclose(f); return false; }
      if (n == 0 && std::feof(f))   break;
      bytes += n;

      std::string_view block(buf.data(), n);
      std::size_t start = 0;
      while (true) {
        std::size_t pos = block.find('\n', start);
        const bool hit_nl = (pos != std::string_view::npos);
        std::string_view slice = hit_nl ? block.substr(start, pos - start)
                                        : block.substr(start);

        if (skipping_oversize) {
          // Keep discarding until newline
          if (hit_nl) { skipping_oversize = false; }
          if (hit_nl) { start = pos + 1; continue; } else break;
        }

        // Append or emit
        if (!hit_nl) {
          // unfinished line → check guard
          if (carry.size() + slice.size() > cfg.max_record_bytes) {
            // overflow behavior
            if (cfg.drop_oversize) {
              skipping_oversize = true; // drop remainder of this logical line
              carry.clear();
            } else {
              // truncate and emit as best-effort
              size_t left = cfg.max_record_bytes - carry.size();
              carry.append(slice.substr(0, left));
              std::string_view out(carry.data(), carry.size());
              if (cfg.strip_cr && !out.empty() && out.back() == '\r') out.remove_suffix(1);
              cb(out);
              carry.clear();
              skipping_oversize = true; // still drop rest until newline
            }
          } else {
            carry.append(slice);
          }
          break;
        }

        // We have a full line
        if (!carry.empty()) {
          carry.append(slice);
          std::string_view out(carry.data(), carry.size());
          if (cfg.strip_cr && !out.empty() && out.back() == '\r') out.remove_suffix(1);
          cb(out);
          carry.clear();
        } else {
          std::string_view out = slice;
          if (cfg.strip_cr && !out.empty() && out.back() == '\r') out.remove_suffix(1);
          cb(out);
        }

        start = pos + 1;
      }
    }

    if (!carry.empty() && !skipping_oversize) {
      std::string_view out(carry.data(), carry.size());
      if (cfg.strip_cr && !out.empty() && out.back() == '\r') out.remove_suffix(1);
      cb(out);
      carry.clear();
    }

    std::fclose(f);
    return true;
  }
};

ChunkReader::ChunkReader(std::string path)
  : ChunkReader(std::move(path), Config{}) {}

ChunkReader::ChunkReader(std::string path, Config cfg)
  : p_(new Impl{std::move(path), cfg}) {}

ChunkReader::~ChunkReader() { delete p_; }

bool ChunkReader::for_each_line(const LineCallback& cb) { return p_->for_each_line(cb); }
int  ChunkReader::last_error() const noexcept { return p_->last_errno; }
std::uint64_t ChunkReader::bytes_read() const noexcept { return p_->bytes; }

bool ChunkReader::read_next(std::string_view& out) {
  bool got = false;
  (void)for_each_line([&](std::string_view s){ if (!got) { out = s; got = true; } });
  return got;
}

}
