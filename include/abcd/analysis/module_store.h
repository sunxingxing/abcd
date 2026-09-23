#ifndef ABCD_ANALYSIS_MODULE_STORE_H_
#define ABCD_ANALYSIS_MODULE_STORE_H_

#include "abcd/analysis/module_load.h"

namespace abcd::analysis {
struct ModuleStore {
  ModuleSlot slot;
};
enum class ModuleStoreError { kUnsupportedOpcode, kMalformedInstruction };
// Pinned v7.0 STMODULEVAR writes the accumulator into a local-export slot and
// preserves the accumulator on normal completion. It is neither a string-pool
// index nor proof that a source declaration can be hoisted before this write.
[[nodiscard]] std::variant<ModuleStore, ModuleStoreError> LiftModuleStore(
    const abc::DecodedInstruction& instruction);
}  // namespace abcd::analysis

#endif  // ABCD_ANALYSIS_MODULE_STORE_H_
