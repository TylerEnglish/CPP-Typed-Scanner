#include "typed_scanner/token_jsonl_simdjson.hpp"
#include "typed_scanner/arena.hpp"
#include "typed_scanner/record_view.hpp"

#include <simdjson.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <cstdio>

namespace ts {

static std::string_view sv_from_number(Arena& arena, double x) {
  char tmp[64];
  int n = std::snprintf(tmp, sizeof(tmp), "%.17g", x);
  return arena.copy(std::string_view(tmp, (n > 0) ? static_cast<size_t>(n) : 0));
}

static std::string_view copy_capped(Arena& arena, std::string_view s, size_t cap) {
  if (s.size() <= cap) return arena.copy(s);
  if (cap <= 3) return arena.copy(s.substr(0, cap));
  std::string out; out.reserve(cap);
  out.append(s.substr(0, cap - 3));
  out.append("...");
  return arena.copy(out);
}

struct JsonlTokenizer::Impl {
  JsonlConfig cfg;
  Arena& header_arena; // owns header keys (interned)
  Arena& row_arena;    // per-line values

  std::vector<std::string_view> header;
  std::vector<std::string_view> fields;

  Impl(const JsonlConfig& c, Arena& ha, Arena& ra)
    : cfg(c), header_arena(ha), row_arena(ra) {}
};

JsonlTokenizer::JsonlTokenizer(const JsonlConfig& cfg, Arena& header_arena, Arena& row_arena)
  : p_(new Impl(cfg, header_arena, row_arena)) {}

const std::vector<std::string_view>& JsonlTokenizer::header() const { return p_->header; }

bool JsonlTokenizer::feed_line(std::string_view line, const RecordCallback& on_record) {
  p_->fields.clear();

  // thread-local scratch and parser
  thread_local simdjson::ondemand::parser parser;
  thread_local std::string scratch;

  scratch.assign(line.data(), line.size());
  scratch.resize(line.size() + simdjson::SIMDJSON_PADDING, '\0');
  simdjson::padded_string_view view(scratch.data(), line.size(), scratch.capacity());

  try {
    auto doc  = parser.iterate(view);
    auto root = doc.get_value(); // treat the document uniformly as a value
    auto t    = root.type().value();

    if (t == simdjson::ondemand::json_type::object) {
      // Single pass over object: build header (if needed) and collect kv pairs
      std::vector<std::pair<std::string_view, std::string_view>> kvs;
      kvs.reserve(16);

      simdjson::ondemand::object obj = root.get_object();
      for (auto field : obj) {
        auto k = field.unescaped_key().value_unsafe();
        std::string_view ksv = p_->cfg.intern_keys
          ? p_->header_arena.copy(std::string_view(k.data(), k.size()))
          : p_->row_arena.copy(std::string_view(k.data(), k.size()));

        if (p_->cfg.intern_keys && p_->header.empty()) {
          // First object seen → capture header order
          p_->header.emplace_back(ksv);
        }

        simdjson::ondemand::value v = field.value();
        std::string_view val_sv{};
        switch (v.type()) {
          case simdjson::ondemand::json_type::number: {
            double x = double(v.get_number().value()); // v3: get_number()
            val_sv = sv_from_number(p_->row_arena, x);
            break;
          }
          case simdjson::ondemand::json_type::string: {
            auto s = v.get_string().value_unsafe();
            val_sv = p_->row_arena.copy(std::string_view(s.data(), s.size()));
            break;
          }
          case simdjson::ondemand::json_type::boolean: {
            bool b = bool(v.get_bool());
            val_sv = p_->row_arena.copy(b ? std::string_view("true") : std::string_view("false"));
            break;
          }
          case simdjson::ondemand::json_type::null: {
            val_sv = std::string_view{};
            break;
          }
          default: {
            // arrays/objects: cap their raw token
            auto tok = v.raw_json_token();
            val_sv = copy_capped(p_->row_arena, std::string_view(tok.data(), tok.length()),
                                 p_->cfg.cap_nested_value_bytes);
            break;
          }
        }

        kvs.emplace_back(ksv, val_sv);
      }

      // (3) Align values by header order if present; else preserve discovery order
      p_->fields.clear();
      if (!p_->header.empty()) {
        p_->fields.reserve(p_->header.size());
        for (auto hk : p_->header) {
          std::string_view val{};
          for (auto &kv : kvs) if (kv.first == hk) { val = kv.second; break; }
          p_->fields.push_back(val);
        }
      } else {
        p_->fields.reserve(kvs.size());
        for (auto &kv : kvs) p_->fields.push_back(kv.second);
      }

      RecordView rv(p_->header.empty() ? nullptr : &p_->header, &p_->fields);
      on_record(rv);
      return true;
    }

    // Non-object line handling
    if (p_->cfg.strict) {
      err_ = "JSONL strict mode: non-object line";
      return false;
    }

    // Lenient scalar/array → 1-field record
    std::string_view val{};
    switch (t) {
      case simdjson::ondemand::json_type::number: {
        double x = double(root.get_number().value());
        val = sv_from_number(p_->row_arena, x);
        break;
      }
      case simdjson::ondemand::json_type::string: {
        auto s = root.get_string().value_unsafe();
        val = p_->row_arena.copy(std::string_view(s.data(), s.size()));
        break;
      }
      case simdjson::ondemand::json_type::boolean: {
        bool b = bool(root.get_bool());
        val = p_->row_arena.copy(b ? std::string_view("true") : std::string_view("false"));
        break;
      }
      case simdjson::ondemand::json_type::null: {
        val = std::string_view{};
        break;
      }
      default: {
        auto tok = root.raw_json_token().value(); // std::string_view
        val = copy_capped(p_->row_arena, tok, p_->cfg.cap_nested_value_bytes);
        break;
      }
    }

    p_->fields.clear();
    p_->fields.push_back(val);
    RecordView rv(nullptr, &p_->fields);
    on_record(rv);
    return true;

  } catch (const std::exception& e) {
    err_ = e.what();
    return false;
  }
}

}
