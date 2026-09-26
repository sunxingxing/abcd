#ifndef ABCD_SOURCE_BINDING_PLAN_H_
#define ABCD_SOURCE_BINDING_PLAN_H_

#include "evaluation_plan.h"
#include "parameter_plan.h"

namespace abcd::source {
struct UnboundValue {};
struct ReceiverBinding {};
using ValueBinding = std::variant<UnboundValue, SymbolId, ReceiverBinding>;
struct ParameterBinding { std::uint32_t index; };
struct SnapshotBinding {
  analysis::ValueId value;
  analysis::OpId producer;
  TemporaryReason reason;
};
struct PhiBinding { analysis::ValueId value; analysis::WorkBlockId block; };
struct CycleBinding { std::uint32_t bundle; CopyTempId temporary; };
struct CellBinding { CellId cell; };
using BindingSource = std::variant<ParameterBinding, SnapshotBinding, PhiBinding, CycleBinding, CellBinding>;
struct BindingPlan {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<ValueBinding> values;
  // Parameters precede value snapshots/phis, per-bundle cycle temps and cells,
  // including unused formal parameters.
  // Names and lexical declarations are assigned by tree construction, not SSA.
  std::vector<BindingSource> symbols;
  // Optional first cycle-temp symbol per incoming bundle, followed by that
  // bundle's dense CopyTempIds. No temporary is shared between edge bundles.
  std::vector<std::optional<SymbolId>> cycle_bases;
  // CellId-indexed mutable storage, separate from every loaded SSA value and
  // snapshot. Declaration scope/position belongs to tree construction.
  std::vector<SymbolId> cells;
  std::uint32_t parameter_count = 0;
  ParameterPlan parameters;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class BindingError : std::uint8_t {
  kEvaluation, kParameter, kUnsupportedEntry, kLivePhi, kDefinition,
  kResourceLimit, kWorkLimit
};
// Requires imported ABI facts and a verified evaluation forest. Constructors
// use this same mapping; receiver initialization legality belongs to tree proof.
[[nodiscard]] std::variant<BindingPlan, BindingError> BuildBindingPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    std::uint32_t parameter_count, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
