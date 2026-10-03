#ifndef ABCD_SOURCE_MODULE_STATE_FLOW_H_
#define ABCD_SOURCE_MODULE_STATE_FLOW_H_
#include "abcd/analysis/module_initialization.h"
#include "module_guards.h"
namespace abcd::source {
// Current graph only; unchanged original proof does not certify moved stores.
// Reuse the exact same abstract SSA state analysis and validate all live
// writes.
std::variant<analysis::ModuleInitialization, ModuleGuardError>
BuildWorkingModuleState(const analysis::ValueProgram&,
                        analysis::SemanticBudget);
}  // namespace abcd::source
#endif
