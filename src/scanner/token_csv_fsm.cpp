#include "typed_scanner/token_csv_fsm.hpp"
#include "typed_scanner/arena.hpp"
#include "typed_scanner/record_view.hpp"
#include <string_view>
#include <vector>

namespace ts {

struct CsvState {
  std::vector<std::string_view> header;
  std::vector<std::string_view> fields;
  bool has_header_emitted{false};
};

struct CsvFsm::Impl {
  CsvConfig cfg;
  Arena& header_arena;
  Arena& row_arena;
  CsvState st;

  bool parse_line(std::string_view line) {
    st.fields.clear();
    // Copy line to the row arena to create stable storage for slicing
    std::string_view buf = row_arena.copy(line);
    const char* s = buf.data();
    const char* e = s + buf.size();

    enum class Mode { Unquoted, Quoted, QuoteEscape } mode = Mode::Unquoted;
    const char* field_start = s;
    for (const char* p = s; p <= e; ++p) {
      char c = (p < e) ? *p : cfg.delimiter; // sentinel delimiter at end
      switch (mode) {
        case Mode::Unquoted:
          if (c == cfg.delimiter || p == e) {
            st.fields.emplace_back(field_start, p - field_start);
            field_start = p + 1;
          } else if (c == cfg.quote) {
            mode = Mode::Quoted;
            field_start = p + 1;
          }
          break;
        case Mode::Quoted:
          if (c == cfg.quote) mode = Mode::QuoteEscape;
          break;
        case Mode::QuoteEscape:
          if (c == cfg.quote) {
            mode = Mode::Quoted;            // escaped quote
          } else if (c == cfg.delimiter || p == e) {
            st.fields.emplace_back(field_start, (p - 1) - field_start); // exclude last quote
            field_start = p + 1;
            mode = Mode::Unquoted;
          } else {
            return false; // malformed
          }
          break;
      }
    }
    return true;
  }
};

CsvFsm::CsvFsm(const CsvConfig& cfg, Arena& header_arena, Arena& row_arena)
  : p_(new Impl{cfg, header_arena, row_arena}), rows_(0) {}

const std::vector<std::string_view>& CsvFsm::header() const { return p_->st.header; }

bool CsvFsm::feed(std::string_view line, const RecordCallback& on_record) {
  if (!p_->parse_line(line)) { err_ = "CSV parse error (quoted field mismatch)"; return false; }

  if (p_->cfg.header && !p_->st.has_header_emitted) {
    // Copy header tokens into the header_arena so they survive row_arena resets.
    p_->st.header.clear();
    p_->st.header.reserve(p_->st.fields.size());
    for (auto sv : p_->st.fields) p_->st.header.emplace_back(p_->header_arena.copy(sv));
    p_->st.has_header_emitted = true;
    return true;
  }

  RecordView rv(&p_->st.header, &p_->st.fields);
  on_record(rv);
  ++rows_;
  return true;
}

bool CsvFsm::finish(const RecordCallback&) { return true; }
CsvFsm::~CsvFsm() { delete p_; }

}
