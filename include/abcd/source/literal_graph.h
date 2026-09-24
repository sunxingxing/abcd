#ifndef ABCD_SOURCE_LITERAL_GRAPH_H_
#define ABCD_SOURCE_LITERAL_GRAPH_H_

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "abcd/analysis/module_load.h"

namespace abcd::source {
struct StaticUndefined {};
struct StaticNull {};
struct StaticFloat {
  std::uint64_t bits;
};
using StaticAtom = std::variant<StaticUndefined, StaticNull, bool,
                                std::int32_t, StaticFloat, std::u16string>;
struct RecordNodeRef {
  std::size_t id = 0;
};
using RecordLiteralValue = std::variant<StaticAtom, RecordNodeRef>;
struct RecordLiteralField {
  std::u16string key;
  RecordLiteralValue value;
};
enum class RecordLiteralKind { kObject, kArray, kImportedConstruct };
struct RecordLiteralNode {
  RecordLiteralKind kind;
  std::vector<RecordLiteralField> fields;
  std::vector<RecordLiteralValue> elements;
  bool attached = false;
  bool emitted = false;
  bool contains_construct = false;
  analysis::ModuleSlot imported_slot{};
};
}  // namespace abcd::source

#endif  // ABCD_SOURCE_LITERAL_GRAPH_H_
