#include "abcd/analysis/isa_catalog.h"

#include <algorithm>
#include <array>

namespace abcd::analysis {
namespace {
#include "abcd/abc/generated_isa_catalog.inc"
}
std::span<const IsaSpec> PinnedIsaCatalog() { return kIsaSpecs; }
const IsaSpec* FindPinnedInstruction(abc::Opcode opcode) {
  const auto found = std::lower_bound(kIsaSpecs.begin(), kIsaSpecs.end(), opcode,
      [](const IsaSpec& entry, abc::Opcode key) { return entry.opcode < key; });
  return found != kIsaSpecs.end() && found->opcode == opcode ? &*found : nullptr;
}
}  // namespace abcd::analysis
