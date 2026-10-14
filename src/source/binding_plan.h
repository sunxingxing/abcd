#ifndef ABCD_SOURCE_BINDING_PLAN_H_
#define ABCD_SOURCE_BINDING_PLAN_H_

#include "evaluation_plan.h"
#include "parameter_plan.h"

namespace abcd::source {
struct ForInEmissionView;
struct ForInKeyBinding { std::uint32_t loop; };
struct UnboundValue {};
struct ReceiverEntryBinding {};
struct NativeReceiverBinding { ReceiverValue source; };
using ValueBinding = std::variant<UnboundValue, SymbolId, ReceiverEntryBinding, NativeReceiverBinding>;
struct InvocationRestBinding {};
struct ParameterBinding { std::uint32_t index; };
struct SnapshotBinding {
  analysis::ValueId value;
  analysis::OpId producer;
  TemporaryReason reason;
};
struct PhiBinding { analysis::ValueId value; analysis::WorkBlockId block; };
struct CycleBinding { std::uint32_t bundle; CopyTempId temporary; };
struct CellBinding { CellId cell; };
struct FunctionSelfBinding {};
struct HoleTokenBinding {};
struct UpdateBinding { analysis::OpId producer; };
struct CatchParameterBinding { analysis::ValueId value; analysis::WorkBlockId block; };
struct CatchSlotBinding { CatchSlotId slot; };
using BindingSource = std::variant<ParameterBinding, InvocationRestBinding, SnapshotBinding, PhiBinding, CycleBinding, CellBinding,
                                   FunctionSelfBinding, CatchParameterBinding, CatchSlotBinding, HoleTokenBinding, UpdateBinding, ForInKeyBinding>;
// These source bindings already hold invocation-owned values. Loaded mutable
// cells, phi/cycle locations and effect snapshots never qualify.
inline bool IsInvocationBinding(const BindingSource& source) {
  return std::holds_alternative<ParameterBinding>(source) ||
      std::holds_alternative<InvocationRestBinding>(source) ||
      std::holds_alternative<FunctionSelfBinding>(source);
}
struct BindingPlan {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<ValueBinding> values;
  // Parameters precede optional self, value snapshots/phis/catch parameters,
  // cycle temps, cells and catch staging slots, including unused formals.
  // Names and lexical declarations are assigned by tree construction, not SSA.
  std::vector<BindingSource> symbols;
  // Optional first cycle-temp symbol per incoming bundle, followed by that
  // bundle's dense CopyTempIds. No temporary is shared between edge bundles.
  std::vector<std::optional<SymbolId>> cycle_bases;
  // CellId-indexed mutable storage, separate from every loaded SSA value and
  // snapshot. Declaration scope/position belongs to tree construction.
  std::vector<SymbolId> cells;
  std::uint32_t parameter_count = 0; // Physical frame count.
  ParameterForm parameter_form = ParameterForm::kFrameFormals;
  std::uint32_t SourceParameterCount() const {
    return parameter_form == ParameterForm::kInvocationRest ? 1U : parameter_count;
  }
  ParameterPlan parameters;
  std::optional<SymbolId> self;
  std::optional<SymbolId> hole;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  EvaluationFlow flow = EvaluationFlow::kNormal;
  // CatchSlotId indexed private storage, never a phi/cell/parameter alias.
  std::vector<SymbolId> catch_slots;
  // OpId indexed: the same producer's private result snapshot, or exclusive
  // scratch when that result has no storage. Never borrow input/parameter/
  // cell/phi/catch/another producer storage. Publication preserves the result.
  std::vector<std::optional<SymbolId>> updates;
};
enum class BindingError : std::uint8_t {
  kEvaluation, kParameter, kUnsupportedEntry, kLivePhi, kDefinition,
  kResourceLimit, kWorkLimit
};
// Requires imported ABI facts and a verified evaluation forest. Constructors
// use this same mapping; receiver initialization legality belongs to tree proof.
[[nodiscard]] std::variant<BindingPlan, BindingError> BuildBindingPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    std::uint32_t parameter_count, analysis::SemanticBudget budget = {},
    ParameterForm parameters = ParameterForm::kFrameFormals, const ForInEmissionView* enumeration = nullptr);
// Catch parameters retain their Catch block identity for lexical handler-scope
// assignment. Staging slots are distinct invocation-private compiler symbols.
// This allocation does not authorize hoisting a catch parameter into the body.
[[nodiscard]] std::variant<BindingPlan, BindingError> BuildPhasedBindingPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    std::uint32_t parameter_count, analysis::SemanticBudget budget = {},
    ParameterForm parameters = ParameterForm::kFrameFormals, const ForInEmissionView* enumeration = nullptr);
}  // namespace abcd::source
#endif
