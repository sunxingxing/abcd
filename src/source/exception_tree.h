#ifndef ABCD_SOURCE_EXCEPTION_TREE_H_
#define ABCD_SOURCE_EXCEPTION_TREE_H_

#include "exception_control.h"
#include "function_tree.h"

namespace abcd::source {
struct ExceptionTreeInput {
  const abc::MethodBody& body;
  // Copy the small borrowed-view descriptor; its referents remain owner-held.
  analysis::PhasedValueInput input;
  const ExceptionPlan& regions;
  const ExceptionControl& control;
};
// Resolve projection provenance at the AST boundary. A projected arc number is
// never an original EdgeId; a try-wrapper continuation has no bytecode edge.
inline std::optional<TreeOrigin> ExceptionArcOrigin(const ExceptionTreeInput& input,
    ControlArcId projected) {
  if (projected.value >= input.control.arcs.size() ||
      projected.value >= input.control.skeleton.edges.size()) return {};
  if (const auto original = input.control.arcs[projected.value].edge) return EdgeOrigin{*original};
  const auto source = input.control.skeleton.edges[projected.value].owner;
  if (source.value >= input.control.units.size()) return {};
  const auto* region = std::get_if<TryControlUnit>(&input.control.units[source.value]);
  if (region) return TryOrigin{region->region};
  if (const auto* exit = std::get_if<TryExitUnit>(&input.control.units[source.value])) {
    if (const auto* owner = std::get_if<analysis::TryId>(&exit->source)) return TryOrigin{*owner};
  }
  return {};
}
inline std::optional<analysis::WorkBlockId> ExceptionNodeBlock(const ExceptionTreeInput& input,
    ControlNodeId projected) {
  if (projected.value >= input.control.units.size()) return {};
  const auto& unit = input.control.units[projected.value];
  if (const auto* phase = std::get_if<PhaseControlUnit>(&unit)) return phase->block;
  if (const auto* region = std::get_if<TryControlUnit>(&unit))
    return input.regions.regions[region->region.value].body;
  return {};
}
// Deliberately distinct from a publication receipt: original phase/control,
// evaluation and independent tree correspondence are checked. Callable ABI,
// same-file constant resolution and family enclosure still need admission.
// No VerifiedFunction or source receipt follows from this draft alone.
struct ExceptionFunctionDraft {
  FunctionTree tree;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  std::optional<TreeFlow> flow;
};
[[nodiscard]] std::variant<ExceptionFunctionDraft, FunctionTreeError>
BuildExceptionFunctionDraft(const analysis::ValueProgram& graph,
    const ExceptionTreeInput& exceptions, const EvaluationPlan& evaluation,
    FunctionSyntax syntax, ExpressionConstants constants,
    analysis::SemanticBudget budget = {}, const IncomingCaptureTree* incoming = nullptr);
}  // namespace abcd::source
#endif
