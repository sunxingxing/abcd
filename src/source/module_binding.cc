#include "module_binding.h"

#include <new>
#include <stdexcept>

#include "abcd/analysis/hole_guard.h"
#include "abcd/analysis/module_load.h"

namespace abcd::source {
namespace {
constexpr std::uint64_t kValidationPasses = 2;
BodyEmitFailure Fail(BodyEmitError kind, abc::CodeOffset offset = {}) {
  return {kind, offset, {}, {}};
}
}  // namespace

std::variant<GuardedModuleLoads, BodyEmitFailure> ValidateGuardedModuleLoads(
    const abc::MethodBody& body, const MethodConstants* constants,
    std::size_t bytes, std::uint64_t work_limit) {
  try {
    bool has_load = false;
    for (const auto& instruction : body.instructions) {
      const auto load = analysis::LiftModuleLoad(instruction.decoded);
      has_load |= std::holds_alternative<analysis::ModuleLoad>(load);
    }
    GuardedModuleLoads result;
    if (!has_load) return result;
    if (!constants || !constants->module)
      return Fail(BodyEmitError::kMissingModule);
    const auto& module = *constants->module;
    if (module.imports.size() > SIZE_MAX - module.namespace_imports.size())
      return Fail(BodyEmitError::kResourceLimit);
    const auto slots = module.imports.size() + module.namespace_imports.size();
    if (body.instructions.size() > bytes)
      return Fail(BodyEmitError::kResourceLimit);
    // Two linear scans. The guard's name pool is intentionally not read:
    // pinned v7.0 resolves it only on the exceptional Hole path.
    if (body.instructions.size() > work_limit / kValidationPasses)
      return Fail(BodyEmitError::kWorkLimit);
    result.work_units = body.instructions.size() * kValidationPasses;
    result.admitted.resize(body.instructions.size());
    for (std::size_t i = 0; i < body.instructions.size(); ++i) {
      const auto load = analysis::LiftModuleLoad(body.instructions[i].decoded);
      const auto* loaded = std::get_if<analysis::ModuleLoad>(&load);
      if (!loaded) continue;
      const auto offset = body.instructions[i].offset;
      if (loaded->space == analysis::ModuleSpace::kExternal) {
        if (loaded->slot.value >= slots)
          return Fail(BodyEmitError::kModuleBinding, offset);
        const auto request = loaded->slot.value < module.imports.size()
                                 ? module.imports[loaded->slot.value].request
                                 : module.namespace_imports[loaded->slot.value -
                                                            module.imports.size()]
                                       .request;
        if (request.value >= module.requests.size())
          return Fail(BodyEmitError::kModuleBinding, offset);
      } else if (!constants->global_scope ||
                 loaded->slot.value >=
                     constants->global_scope->module_slots ||
                 loaded->slot.value >= module.local_exports.size()) {
        return Fail(BodyEmitError::kModuleBinding, offset);
      }
      if (i + 1 >= body.instructions.size())
        return Fail(BodyEmitError::kModuleBinding, offset);
      const auto checked = analysis::LiftHoleInstruction(
          body.instructions[i + 1].decoded, 0);
      const auto* guard = std::get_if<analysis::HoleGuard>(&checked);
      if (!guard ||
          !std::holds_alternative<analysis::Accumulator>(guard->value) ||
          !std::holds_alternative<analysis::HoleNameSlot>(guard->name))
        return Fail(BodyEmitError::kModuleBinding, offset);
      result.admitted[i] = 1;
    }
    return result;
  } catch (const std::bad_alloc&) {
    return Fail(BodyEmitError::kResourceLimit);
  } catch (const std::length_error&) {
    return Fail(BodyEmitError::kResourceLimit);
  }
}
}  // namespace abcd::source
