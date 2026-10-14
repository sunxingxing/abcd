#ifndef ABCD_SOURCE_TREE_FAILURE_H_
#define ABCD_SOURCE_TREE_FAILURE_H_
#include "abcd/source/source_ids.h"
#include "abcd/source/control_failure.h"
#include "abcd/source/capture_occurrence.h"
#include "abcd/analysis/value_ir.h"
#include "abcd/source/evaluation_kind.h"
namespace abcd::source {
// Diagnostic obligations only: these tags never grant recovery admission.
enum class TreeObligation : std::uint8_t {
  kControlVerification, kCompletion, kDecisionEdgeCopies, kDecisionPrefix,
  kCatchScope, kCatchStaging, kExpressionOwnership, kDeferredDeclaration,
  kStructure, kFlow, kSemantics, kBindings, kCapture, kPlacement, kForInCorrespondence, kForInRecovery
};
struct TreeSyntheticLocation {
  ControlOriginKind kind;
  std::uint32_t source;
  bool operator==(const TreeSyntheticLocation&) const = default;
};
struct TreeLocation {
  std::optional<abc::EntityOffset> method;
  std::optional<analysis::OpId> operation;
  std::optional<analysis::WorkBlockId> block;
  std::optional<ControlId> control;
  std::optional<analysis::EdgeId> edge;
  std::optional<ExprId> expression;
  std::optional<StmtId> statement;
  std::optional<TreeSyntheticLocation> synthetic;
  std::optional<abc::CodeOffset> pc, working_pc;
  bool operator==(const TreeLocation&) const = default;
};
enum class ExpressionOwnershipError : std::uint8_t { kMissingExpression, kPlacement };
struct TreeFailureEvidence {
  TreeObligation obligation = TreeObligation::kExpressionOwnership;
  TreeLocation location;
  std::optional<TreeLocation> expected_owner, actual_owner;
  std::optional<ExpressionOwnershipError> ownership;
  std::optional<EvaluationKind> placement;
  std::optional<TreeLocation> decision_entry;
  bool operator==(const TreeFailureEvidence&) const = default;
};
enum class DeferredCellError : std::uint8_t { kExternalRoot, kPhasedParent, kPlacement, kBinding };
struct DeferredCellEvidence {
  DeferredCellError reason;
  std::optional<abc::EntityOffset> creator, child;
  std::optional<CaptureOccurrence> occurrence;
  std::optional<CellId> cell;
  std::optional<analysis::CaptureDomain> requested;
  TreeLocation creation, allocation, store;
};
// O(1), no allocation. Working coordinates never masquerade as original PCs.
inline TreeLocation OperationLocation(const analysis::ValueProgram& graph, analysis::OpId id,
    std::optional<abc::EntityOffset> method = {}) {
  TreeLocation result; result.method = method; result.operation = id;
  if (id.value < graph.ops.size()) {
    const auto& op = graph.ops[id.value]; result.block = op.block;
    if (op.origin.value < graph.origins.size()) {
      const auto& origin = graph.origins[op.origin.value];
      result.working_pc = origin.pc; result.pc = origin.original.value_or(origin.pc);
    }
  }
  return result;
}
} // namespace abcd::source
#endif
