#include "abcd/analysis/access_catalog.h"

#include <algorithm>
#include <array>

namespace abcd::analysis {
namespace {
#include "abcd/abc/generated_access_catalog.inc"
}
std::span<const AccessSpec> PinnedAccessCatalog() { return kAccessSpecs; }
const AccessSpec* FindPinnedAccess(abc::Opcode opcode) {
  const auto found = std::lower_bound(kAccessSpecs.begin(), kAccessSpecs.end(), opcode,
      [](const AccessSpec& entry, abc::Opcode key) { return entry.opcode < key; });
  return found != kAccessSpecs.end() && found->opcode == opcode ? &*found : nullptr;
}
}  // namespace abcd::analysis
