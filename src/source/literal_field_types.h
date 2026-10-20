#ifndef ABCD_SOURCE_LITERAL_FIELD_TYPES_H_
#define ABCD_SOURCE_LITERAL_FIELD_TYPES_H_
#include "abcd/analysis/value_ir.h"
namespace abcd::source {
struct LiteralFieldRange {
  std::size_t first = 0, count = 0;
  bool operator==(const LiteralFieldRange&) const = default;
};
struct LiteralFieldObject {
  analysis::OpId allocation, last, call, callee;
  analysis::UseId argument;
  bool operator==(const LiteralFieldObject&) const = default;
};
// Prefix roots retain their original SSA snapshots. The native property value
// sequences them at their original point, not before the saved member lookup.
struct LiteralFields {
  std::vector<LiteralFieldObject> objects;
  std::vector<LiteralFieldRange> fields;
  std::vector<analysis::OpId> prefixes;
  std::vector<std::optional<analysis::OpId>> prefix_fields, members;
  bool operator==(const LiteralFields&) const = default;
};
}  // namespace abcd::source
#endif
