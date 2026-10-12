#include "capture_plan.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
bool SameCell(const CaptureCell& left, const CaptureCell& right) {
  if (const auto* local = std::get_if<analysis::LocalCell>(&left)) {
    const auto* other = std::get_if<analysis::LocalCell>(&right);
    return other && local->environment == other->environment && local->slot.value == other->slot.value;
  }
  const auto& outer = std::get<analysis::CapturedSlot>(left);
  const auto* other = std::get_if<analysis::CapturedSlot>(&right);
  return other && outer.depth == other->depth && outer.slot.value == other->slot.value;
}
}  // namespace
std::variant<std::uint64_t, CaptureFailure> VerifyCapturePlan(
    const ScopeFacts& scopes, analysis::InstructionId site,
    const CaptureSite& creation, std::span<const CaptureUse> uses,
    const CapturePlan& plan, analysis::SemanticBudget budget) {
  if (budget.work == 0) return CaptureError::kWorkLimit;
  auto work = budget.work - 1;
  if (!scopes.Matches(site, creation) || plan.site != site || plan.pc != CapturePc(creation) ||
      plan.target != CapturePosition(creation)) return CaptureError::kCreation;
  if (uses.size() != plan.bindings.size()) return CaptureError::kCoverage;
  if (scopes.Writes() != analysis::ClosureWrites::kUnknown &&
      scopes.Writes() != analysis::ClosureWrites::kLanguage) return CaptureError::kScope;
  if (plan.writes != scopes.Writes()) return CaptureError::kScope;
  const auto query = scopes.QueryWork();
  for (std::size_t i = 0; i < uses.size(); ++i) {
    if (query > work) return CaptureError::kWorkLimit;
    work -= query;
    const auto resolved = scopes.Rebase(site, uses[i].cell, uses[i].domain);
    const auto* cell = std::get_if<CaptureCell>(&resolved);
    if (!cell) return CaptureFailure{CaptureError::kScope, std::get<ScopeError>(resolved)};
    if (uses[i].domain != plan.bindings[i].domain || !SameCell(*cell, plan.bindings[i].cell))
      return CaptureError::kBinding;
  }
  return budget.work - work;
}
std::variant<CapturePlan, CaptureFailure> BuildCapturePlan(
    const ScopeFacts& scopes, analysis::InstructionId site,
    const CaptureSite& creation, std::span<const CaptureUse> uses,
    analysis::SemanticBudget budget) {
  try {
    if (budget.work == 0) return CaptureError::kWorkLimit;
    auto work = budget.work - 1;
    if (!scopes.Matches(site, creation)) return CaptureError::kCreation;
    if (scopes.Writes() != analysis::ClosureWrites::kUnknown &&
        scopes.Writes() != analysis::ClosureWrites::kLanguage) return CaptureError::kScope;
    if (sizeof(CapturePlan) > budget.bytes) return CaptureError::kResourceLimit;
    const auto bytes = budget.bytes - sizeof(CapturePlan);
    if (uses.size() > bytes / sizeof(CaptureBinding)) return CaptureError::kResourceLimit;
    const auto pc = CapturePc(creation);
    if (!pc) return CaptureError::kCreation;
    CapturePlan plan{site, *pc, CapturePosition(creation)};
    plan.writes = scopes.Writes();
    plan.bindings.reserve(uses.size());
    if (plan.bindings.capacity() > bytes / sizeof(CaptureBinding)) return CaptureError::kResourceLimit;
    const auto query = scopes.QueryWork();
    for (const auto& use : uses) {
      if (query > work) return CaptureError::kWorkLimit;
      work -= query;
      const auto resolved = scopes.Rebase(site, use.cell, use.domain);
      const auto* cell = std::get_if<CaptureCell>(&resolved);
      if (!cell) return CaptureFailure{CaptureError::kScope, std::get<ScopeError>(resolved)};
      plan.bindings.push_back({*cell, use.domain});
    }
    const auto checked = VerifyCapturePlan(scopes, site, creation, uses, plan, {bytes, work});
    if (const auto* error = std::get_if<CaptureFailure>(&checked)) return *error;
    work -= std::get<std::uint64_t>(checked);
    plan.work = budget.work - work;
    plan.allocated_bytes = sizeof(plan) + plan.bindings.capacity() * sizeof(CaptureBinding);
    return plan;
  } catch (const std::bad_alloc&) { return CaptureError::kResourceLimit;
  } catch (const std::length_error&) { return CaptureError::kResourceLimit; }
}
}  // namespace abcd::source
