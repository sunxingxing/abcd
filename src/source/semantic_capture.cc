#include "semantic_capture.h"
#include "scope_facts.h"

#include <algorithm>
#include <tuple>
#include "abcd/analysis/exception_locals.h"

namespace abcd::source {
std::variant<analysis::LocalCell, analysis::CapturedSlot, SourceFailure>
ResolveCreationCapture(const analysis::EnvironmentFlow& environments,
    const FunctionExpression& function, analysis::CapturedSlot capture,
    abc::CodeOffset pc, const analysis::ExceptionLocals* locals) {
  const auto site = function.capture_site;
  const SourceFailure failure{SourceError::kCreation, pc};
  if (!site || site->value >= environments.before.size() ||
      !environments.before[site->value]) return failure;
  const auto resolved = environments.environments.ResolveCapture(
      *environments.before[site->value], capture.depth, capture.slot);
  const auto* binding = std::get_if<analysis::LexicalBinding>(&resolved);
  if (!binding) return failure;
  if (const auto* depth = std::get_if<analysis::CapturedDepth>(&binding->environment))
    return analysis::CapturedSlot{*depth, binding->slot};
  if (!locals || site->value >= locals->capture_entries.size()) return failure;
  const auto* owner = std::get_if<analysis::EnvironmentId>(&binding->environment);
  if (!owner) return failure;
  const analysis::LocalCell cell{*owner, binding->slot};
  const auto key = [](auto item) { return std::tuple(item.environment.value, item.slot.value); };
  const auto& cells = locals->plan.cells;
  const auto found = std::lower_bound(cells.begin(), cells.end(), cell,
      [&](auto a, auto b) { return key(a) < key(b); });
  if (found == cells.end() || key(*found) != key(cell)) return failure;
  const auto id = static_cast<std::uint32_t>(found - cells.begin());
  const auto entry = locals->capture_entries[site->value];
  const auto& scopes = locals->capture_scopes;
  const auto scope = std::upper_bound(scopes.begin(), scopes.end(), std::tuple(id, entry),
      [](auto target, const auto& item) {
        return target < std::tuple(item.cell.value, item.begin);
      });
  if (!entry || scope == scopes.begin() || (scope - 1)->cell.value != id ||
      entry >= (scope - 1)->end) return failure;
  return cell;
}
std::variant<analysis::LocalCell, analysis::CapturedSlot, SourceFailure>
ResolveCreationCapture(
    const analysis::SemanticMethod& method,
    const analysis::SemanticProof& proof, analysis::InstructionId creation,
    analysis::CapturedSlot capture, analysis::CaptureDomain requirement) {
  const auto instruction = creation.value;
  const auto fail = [&] {
    return SourceFailure{SourceError::kCreation,
        instruction < method.instructions.size() ? method.instructions[instruction].pc
                                                : abc::CodeOffset{}};
  };
  const auto resolved = ScopeFacts(method, proof).Rebase(creation, capture, requirement);
  const auto* cell = std::get_if<CaptureCell>(&resolved);
  if (!cell) return fail();
  if (const auto* local = std::get_if<analysis::LocalCell>(cell)) return *local;
  return std::get<analysis::CapturedSlot>(*cell);
}
}  // namespace abcd::source
