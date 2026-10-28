#ifndef ABCD_SOURCE_CELL_LIFETIME_H_
#define ABCD_SOURCE_CELL_LIFETIME_H_

#include "cell_plan.h"
#include "control_plan.h"
#include "abcd/analysis/exception_flow.h"

namespace abcd::source {
// Candidate container IDs are NOT FunctionTree ScopeIds; map typed origins at
// construction and independently check actual scope/declaration positions.
struct CellScopeId { std::uint32_t value; auto operator<=>(const CellScopeId&) const = default; };
struct CellBodyScope {};
struct CellArmScope { ControlId branch; analysis::FlowEdgeKind arm; };
struct CellLoopScope { LoopId loop; };
struct CellIteratorScope { IteratorProtocolId protocol; };
enum class CellTryPart : std::uint8_t { kBody, kHandler };
struct CellTryScope { analysis::TryId region; CellTryPart part; };
struct CellSwitchScope { analysis::WorkBlockId entry; };
struct CellSwitchCaseScope { analysis::WorkBlockId entry; std::uint32_t clause; };
struct CellScope {
  std::optional<CellScopeId> parent;
  std::variant<CellBodyScope, CellArmScope, CellLoopScope, CellTryScope, CellSwitchScope, CellSwitchCaseScope, CellIteratorScope> origin;
};
struct IncomingLifetime {};
struct CellLifetimes {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<CellScope> scopes;
  // No source scope for inactive, forwarded-handler or implicit-close phases.
  // Every allocation/access must have an explicit visited source scope.
  std::vector<std::optional<CellScopeId>> blocks;
  // Indexed by CellId. Ordinary declarations stay at allocation. Certified
  // deferred declarations may use a private, nonrepeating descendant scope;
  // incoming bindings still need the creation owner, not body locals.
  std::vector<std::variant<CellScopeId, IncomingLifetime>> cells;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class CellLifetimeError : std::uint8_t {
  kCells, kControl, kScopeEscape, kResourceLimit, kWorkLimit
};
// Native-declaration candidates, NOT an AST certificate. Mirrors only the
// lexical containers required by checked control regions. A declaration is
// never lifted across an arm/loop boundary to make an escaping access fit.
// Initialization and actual declaration/use/capture placement need independent
// source-tree checks before publication; no block/SSA rewrite occurs here.
[[nodiscard]] std::variant<CellLifetimes, CellLifetimeError> BuildCellLifetimes(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& cells, const ControlPlan& control, analysis::SemanticBudget budget = {});
struct ExceptionTreeInput;
struct ForOfEvaluationPlan;
struct ForOfEmissionView;
// For a protocol-owned control projection, both owner-held iteration plan and
// emission view are required. The seal and view are reverified from original
// input; none of these borrowed inputs survives this call.
// Deferred cells additionally require every access/demand inside the unique
// store scope, with no extra loop entry relative to the original allocation.
[[nodiscard]] std::variant<CellLifetimes, CellLifetimeError> BuildPhasedCellLifetimes(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& cells, const ExceptionTreeInput& exceptions,
    analysis::SemanticBudget budget = {},
    const ForOfEvaluationPlan* iteration = nullptr,
    const ForOfEmissionView* view = nullptr,
    std::span<const std::optional<analysis::OpId>> deferred = {});
}  // namespace abcd::source
#endif
