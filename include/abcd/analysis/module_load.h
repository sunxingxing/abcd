#ifndef ABCD_ANALYSIS_MODULE_LOAD_H_
#define ABCD_ANALYSIS_MODULE_LOAD_H_

#include "abcd/abc/instruction.h"

namespace abcd::analysis {
enum class ModuleSpace { kLocal, kExternal };
struct ModuleSlot {
  std::uint16_t value;
  auto operator<=>(const ModuleSlot&) const = default;
};
// OH7 GetSendableModuleValueOuter reads JSFunction::GetModule; ordinary
// external reads use the current module. Keep that identity orthogonal to lazy.
enum class ModuleDomain : std::uint8_t { kOrdinary, kSendable };
enum class ModuleLoading : std::uint8_t { kOrdinary, kLazy };
struct ModuleLoad {
  ModuleSpace space;
  ModuleSlot slot;
  ModuleLoading loading = ModuleLoading::kOrdinary;
  ModuleDomain domain = ModuleDomain::kOrdinary;
  // GETMODULENAMESPACE: `slot` is the module-request index (requestedModules),
  // not a binding slot. Source resolution maps it to the unique namespace
  // import entry of that request; the runtime reads the same namespace object.
  bool request_namespace = false;
};
enum class ModuleLoadError { kUnsupportedOpcode, kMalformedInstruction };
// Pinned v7.0 local/external module value reads. Slot is NOT a string pool or
// module-request index. Normal completion replaces acc, preserving private
// caller registers and frame.thisObj. May throw; may produce Hole. Do not
// delete subsequent initialization guards or assume an ordinary JS value.
[[nodiscard]] std::variant<ModuleLoad, ModuleLoadError> LiftModuleLoad(
    const abc::DecodedInstruction& instruction);
}  // namespace abcd::analysis
#endif
