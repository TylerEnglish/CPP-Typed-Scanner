#include "typed_scanner/path_utils.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#if defined(TS_USE_OPENSSL)
  #include <openssl/sha.h>
#endif

namespace ts {

std::filesystem::path join(const std::filesystem::path& a,
                           const std::filesystem::path& b) {
  return std::filesystem::weakly_canonical(a / b);
}

bool ensure_parent_dirs(const std::filesystem::path& p) {
  std::error_code ec;
  auto parent = p.parent_path();
  if (parent.empty()) return true;
  if (std::filesystem::exists(parent)) return true;
  return std::filesystem::create_directories(parent, ec) || !ec;
}

FileFormat detect_format(std::string_view path) {
  auto ext = std::filesystem::path(std::string(path)).extension().string();
  if (ext == ".csv") return FileFormat::CSV;
  if (ext == ".jsonl" || ext == ".ndjson") return FileFormat::JSONL;
  return FileFormat::Unknown;
}

std::string hex_hash_prefix(std::string_view data, int len) {
#ifdef TS_USE_OPENSSL
  unsigned char md[SHA256_DIGEST_LENGTH];
  SHA256_CTX ctx; SHA256_Init(&ctx);
  SHA256_Update(&ctx, data.data(), data.size());
  SHA256_Final(md, &ctx);
  std::ostringstream o;
  for (int i = 0; i < (len+1)/2 && i < SHA256_DIGEST_LENGTH; ++i)
    o << std::hex << std::setw(2) << std::setfill('0') << (int)md[i];
  auto s = o.str();
  if ((int)s.size() > len) s.resize(len);
  return s;
#else
  // Fallback (non-crypto)
  size_t h = std::hash<std::string_view>{}(data);
  std::ostringstream o; o << std::hex << h;
  auto s = o.str(); if ((int)s.size() > len) s.resize(len); return s;
#endif
}

std::string make_slug(std::string_view key, std::string_view mode, int len) {
  if (mode == "basename") {
    auto base = std::filesystem::path(std::string(key)).filename().string();
    if ((int)base.size() > len) base.resize(len);
    return base;
  }
  if (mode == "keypath") {
    auto s = std::string(key);
    for (auto& c : s) if (c=='/' || c=='\\') c='-';
    if ((int)s.size() > len) s.resize(len);
    return s;
  }
  // default: hashprefix
  return hex_hash_prefix(key, len);
}

}
