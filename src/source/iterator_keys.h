#ifndef ABCD_SOURCE_ITERATOR_KEYS_H_
#define ABCD_SOURCE_ITERATOR_KEYS_H_

#include "abcd/abc/pool_reference.h"
#include "abcd/analysis/value_ir.h"

namespace abcd::source {
enum class IteratorKey : std::uint8_t { kNext, kDone, kValue, kReturn, kOther };
struct IteratorKeyOrigin {
  abc::PoolIndex pool;
  abc::EntityOffset entity;
  IteratorKey key;
  bool operator==(const IteratorKeyOrigin&) const = default;
};
struct IteratorKeyUse {
  analysis::OpId operation;
  std::uint32_t origin;
  bool operator==(const IteratorKeyUse&) const = default;
};
// Text is classified only at this file boundary. The protocol analysis uses
// enum keys and retained pool provenance, never names or printed operations.
struct IteratorKeys {
  abc::EntityOffset method;
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<IteratorKeyOrigin> origins; // PoolIndex order; aliases retained.
  std::vector<IteratorKeyUse> uses;       // OpId order.
  std::size_t allocated_bytes = 0;
  std::size_t retained_bytes = 0;
  std::uint64_t work = 0;
};
enum class IteratorKeyError : std::uint8_t { kInput, kResourceLimit, kWorkLimit };
using IteratorKeysResult = std::variant<IteratorKeys, IteratorKeyError, abc::MetadataError>;
struct IteratorKeyInput {
  const corpus::AbcFile& file;
  const abc::Header& header;
  const abc::MetadataIndex& index;
};
// Borrowed immutable file/index/graph. Resolves each pool slot once and reads
// each aliased string entity once. O(U log U + string bytes), charged scratch.
// No TreeConstants, source symbols, module binding or name allocation.
[[nodiscard]] IteratorKeysResult ResolveIteratorKeys(const IteratorKeyInput&,
    abc::EntityOffset method, const analysis::ValueProgram&, analysis::SemanticBudget = {});
}  // namespace abcd::source
#endif
