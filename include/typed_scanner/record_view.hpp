#pragma once
#include <string_view>
#include <vector>
#include <utility>
#include <cstddef>

namespace ts {

// Lightweight view over a tokenized record.
// CSV: fields_ are column values; header_ optionally points to column names.
// JSONL: fields_ store flattened "key=value" views OR positional values,
// depending on policy (we'll keep it generic here).
class RecordView {
public:
  RecordView() = default;
  RecordView(const std::vector<std::string_view>* header,
             const std::vector<std::string_view>* fields)
      : header_(header), fields_(fields) {}

  std::size_t size() const noexcept { return fields_ ? fields_->size() : 0; }

  // Get field by index.
  std::string_view at(std::size_t i) const {
    return (fields_ && i < fields_->size()) ? (*fields_)[i] : std::string_view{};
  }

  // Optional header (CSV) — column name for index i (if present).
  std::string_view colname(std::size_t i) const {
    return (header_ && i < header_->size()) ? (*header_)[i] : std::string_view{};
  }

  const std::vector<std::string_view>* header() const noexcept { return header_; }
  const std::vector<std::string_view>* fields() const noexcept { return fields_; }

private:
  const std::vector<std::string_view>* header_{nullptr};
  const std::vector<std::string_view>* fields_{nullptr};
};

} 
