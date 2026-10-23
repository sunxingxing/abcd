#include "scope_facts.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <tuple>

namespace abcd::source {
namespace {
bool Before(const ExceptionScopeFacts& facts, analysis::InstructionId site) {
  if (facts.before.value >= facts.flow.nodes.size() ||
      facts.before.value >= facts.environments.before.size() ||
      !facts.environments.before[facts.before.value]) return false;
  const auto& node = facts.flow.nodes[facts.before.value];
  return node.phase == analysis::ExceptionPhase::kBefore && node.instruction == site &&
      site.value < facts.flow.instructions.size();
}
}  // namespace
std::uint64_t ExceptionScopeFacts::QueryWork() const {
  constexpr auto kChecks = std::numeric_limits<std::uint32_t>::digits;
  return 2 * kChecks + std::bit_width(locals.plan.cells.size()) +
      std::bit_width(locals.capture_scopes.size()) + std::bit_width(locals.deferred_initializations.size());
}
bool ExceptionScopeFacts::Matches(analysis::InstructionId site,
    const analysis::CreationBinding& creation) const {
  const auto* ordinary = std::get_if<analysis::OrdinaryCapture>(&creation.capture);
  if (!ordinary || !Before(*this, site)) return false;
  const auto& instruction = flow.instructions[site.value];
  return CreatesBinding(instruction.operation, creation) && instruction.pc == creation.offset && *environments.before[before.value] == ordinary->environment;
}
bool ExceptionScopeFacts::Matches(analysis::InstructionId site, const ClassCaptureSite& creation) const {
  const auto* expected = CaptureClassCreation(creation);
  if (!expected || !Before(*this, site)) return false;
  const auto& instruction = flow.instructions[site.value];
  const auto* operation = std::get_if<analysis::ClassCreation>(&instruction.operation);
  return operation && *operation == *expected && instruction.pc == CapturePc(creation) &&
      *environments.before[before.value] == creation.environment;
}
std::variant<CaptureCell, ScopeError> ExceptionScopeFacts::Rebase(
    analysis::InstructionId site, analysis::CapturedSlot capture, analysis::CaptureDomain domain,
    std::optional<analysis::InstructionId>* deferred) const {
  if (deferred) deferred->reset();
  using Error = ScopeError;
  switch (domain) {
    case analysis::CaptureDomain::kLanguage:
    case analysis::CaptureDomain::kDefined:
    case analysis::CaptureDomain::kGuarded:
    case analysis::CaptureDomain::kSourceAccess: break;
    default: return Error::kDomain;
  }
  if (!Before(*this, site)) return Error::kSite;
  if (locals.writes != analysis::ClosureWrites::kUnknown &&
      locals.writes != analysis::ClosureWrites::kLanguage) return Error::kWrites;
  const auto resolved = environments.environments.ResolveCapture(
      *environments.before[before.value], capture.depth, capture.slot);
  const auto* binding = std::get_if<analysis::LexicalBinding>(&resolved);
  if (!binding) return Error::kEnvironment;
  if (const auto* depth = std::get_if<analysis::CapturedDepth>(&binding->environment))
    return CaptureCell{analysis::CapturedSlot{*depth, binding->slot}};
  const auto environment = std::get<analysis::EnvironmentId>(binding->environment);
  const auto& plan = locals.plan;
  if (environment.value >= plan.allocations.size() || !plan.allocations[environment.value])
    return Error::kCell;
  const analysis::LocalCell cell{environment, binding->slot};
  const auto key = [](auto value) { return std::tuple(value.environment.value, value.slot.value); };
  const auto found = std::lower_bound(plan.cells.begin(), plan.cells.end(), cell,
      [&](auto left, auto right) { return key(left) < key(right); });
  if (found == plan.cells.end() || key(*found) != key(cell)) return Error::kCell;
  if (before.value >= locals.capture_entries.size()) return Error::kInitialization;
  const auto entry = locals.capture_entries[before.value];
  const auto id = static_cast<std::uint32_t>(found - plan.cells.begin());
  const auto scope = std::upper_bound(locals.capture_scopes.begin(), locals.capture_scopes.end(),
      std::tuple(id, entry), [](auto target, const auto& value) {
        return target < std::tuple(value.cell.value, value.begin);
      });
  if (!entry || scope == locals.capture_scopes.begin() || (scope - 1)->cell.value != id ||
      entry >= (scope - 1)->end) {
    // Guarded reads preserve their Hole throw; SourceAccess uses only the
    // existing approved SourceTdz contract. Language/Defined stay strict.
    const bool guarded = domain == analysis::CaptureDomain::kGuarded ||
        (domain == analysis::CaptureDomain::kSourceAccess && source_tdz);
    if (!deferred || !guarded) return Error::kInitialization;
    const auto& facts = locals.deferred_initializations;
    const auto found = std::lower_bound(facts.begin(), facts.end(), id,
        [](const auto& fact, auto cell) { return fact.cell.value < cell; });
    if (found == facts.end() || found->cell.value != id || !entry ||
        entry < found->begin || entry >= found->end) return Error::kInitialization;
    *deferred = found->store;
  }
  // Completed Language stores do not imply a TDZ guard, immutability or a
  // frozen value. Keep Defined obligations closed until phase guard evidence.
  if (domain == analysis::CaptureDomain::kDefined) return Error::kDomain;
  return CaptureCell{cell};
}
}  // namespace abcd::source
