#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace ts {

class ChunkReader {
public:
  struct Config {
    std::size_t chunk_bytes      = 512 * 1024;      // 512 KiB
    std::size_t max_record_bytes = 8 * 1024 * 1024; // 8 MiB guard per line
    bool        strip_cr         = true;            // trim trailing '\r' (CRLF)
    bool        drop_oversize    = true;            // drop lines exceeding guard
  };

  explicit ChunkReader(std::string path);      // uses default Config{}
  ChunkReader(std::string path, Config cfg);   // explicit Config

  bool read_next(std::string_view& out);

  using LineCallback = std::function<void(std::string_view)>;

  ~ChunkReader();

  bool for_each_line(const LineCallback& cb);
  int  last_error() const noexcept;
  std::uint64_t bytes_read() const noexcept;

private:
  struct Impl; Impl* p_;
};

} 
