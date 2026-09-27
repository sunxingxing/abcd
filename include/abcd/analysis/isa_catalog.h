#ifndef ABCD_ANALYSIS_ISA_CATALOG_H_
#define ABCD_ANALYSIS_ISA_CATALOG_H_

#include <span>

#include "abcd/abc/instruction.h"

namespace abcd::analysis {
// Order follows the pinned OpenHarmony-v7.0-Release isa.yaml groups.
enum class IsaGroup : std::uint8_t {
  kConstant, kIterator, kCreation, kBinary, kUnary, kComparison,
  kRuntimeCall, kThrow, kCall, kDefinition, kObjectAccess, kString,
  kJump, kMove, kRegisterLoad, kRegisterStore, kImmediate, kReturn,
  kNop, kCount
};
struct IsaSpec {
  abc::Opcode opcode;
  IsaGroup group;
  std::uint8_t size;
  bool reads_acc;
  bool writes_acc;
};
// Inventory only: membership never authorizes semantic lifting or publication.
// Static immutable storage; lookup O(log N), no allocation.
[[nodiscard]] std::span<const IsaSpec> PinnedIsaCatalog();
[[nodiscard]] const IsaSpec* FindPinnedInstruction(abc::Opcode opcode);
}  // namespace abcd::analysis
#endif
