#ifndef ABCD_ANALYSIS_REGISTER_VALUE_FLOW_H_
#define ABCD_ANALYSIS_REGISTER_VALUE_FLOW_H_

#include "abcd/analysis/register_ssa.h"
#include "abcd/analysis/arithmetic_semantics.h"
#include "abcd/analysis/call_result.h"
#include "abcd/analysis/frame_receiver.h"
#include "abcd/analysis/value_kinds.h"

namespace abcd::analysis {
struct EnvironmentFlow;
class EnvironmentEscape;
struct LexicalValueContext {
  const EnvironmentFlow& environments;
  const EnvironmentEscape& escape;
};
struct EntryValueFact {
  SsaValueId value;
  ValueKinds kinds;
};
struct LanguageLoadFact {
  InstructionId instruction;
};
enum class ValueFlowError {
  kInvalidSsa,
  kInstruction,
  kInvalidEntryFact,
  kDuplicateEntryFact,
  kInvalidLoadFact,
  kDuplicateLoadFact,
  kInvalidLexicals,
  kWorkLimit,
  kResourceLimit,
  kInvalidSemantics,
  kInvalidPendingFact,
  kDuplicatePendingFact
};
struct ValueFlowFailure {
  ValueFlowError kind;
  std::optional<InstructionId> instruction;
  std::optional<ScalarError> lift;
};
struct ValueFlowWork {
  std::uint64_t changes = 0;
  std::uint64_t queue_visits = 0;
  std::uint64_t edge_visits = 0;
  std::uint64_t dependencies = 0;
  std::uint64_t units = 0;
  std::size_t peak_bytes = 0;
  std::size_t cells = 0;
};

// Requires SSA from this unchanged validated body/frame. Supplied entry facts
// are caller-PROVEN assumptions, never inferred ABI facts. Unspecified entries
// are top. No global Hole-guard refinement, heap/callee proof, code motion or
// source equivalence follows from this value-category result. Normal SSA only.
// Sparse finite-height worklist: each dependency edge visited at most four
// times.
class RegisterValueFlow {
 public:
  RegisterValueFlow(RegisterValueFlow&&) noexcept = default;
  RegisterValueFlow& operator=(RegisterValueFlow&&) noexcept = default;
  [[nodiscard]] static std::variant<RegisterValueFlow, ValueFlowFailure> Build(
      const abc::MethodBody& body, const RegisterSsa& ssa,
      std::uint64_t frame_size, std::span<const EntryValueFact> entries,
      IdfQuota& quota, IdfBudget budget = {});
  // The caller proves that these lexical/module loads return language values
  // on the normal path. The solver validates site kind and SSA ownership;
  // it never infers TDZ or captured-cell initialization from an opcode alone.
  // With semantic instructions, both load-premise validation and value rules
  // consume that unchanged IR, which must also have supplied the SSA effects.
  // The span is borrowed during this call only; no duplicate rule table is kept.
  // Non-default call results require typed semantics. They do not certify any
  // instruction input, lexical/module load, or explicit method return operand.
  [[nodiscard]] static std::variant<RegisterValueFlow, ValueFlowFailure>
  BuildWithLanguageLoads(const abc::MethodBody& body, const RegisterSsa& ssa,
                         std::uint64_t frame_size,
                         std::span<const EntryValueFact> entries,
                         std::span<const LanguageLoadFact> loads,
                         IdfQuota& quota, IdfBudget budget = {},
                         std::span<const SemanticInstruction> semantics = {},
                         CallResultValues calls = CallResultValues::kUnknown,
                         FrameReceiver receiver = FrameReceiver::kUnknown,
      ArithmeticSemantics arithmetic = ArithmeticSemantics::kPinnedRuntime);
  // Same unchanged inputs as the escape proof. Nonescaping local cells join
  // Hole and ALL stores, not reaching stores. Captured/escaped cells stay top.
  [[nodiscard]] static std::variant<RegisterValueFlow, ValueFlowFailure>
  BuildWithLexicals(const abc::MethodBody& body, const RegisterSsa& ssa,
                    std::uint64_t frame_size,
                    std::span<const EntryValueFact> entries,
                    const LexicalValueContext& lexicals, IdfQuota& quota,
                    IdfBudget budget = {});
  [[nodiscard]] std::span<const ValueKinds> Facts() const {
    return {facts_.get(), facts_ ? count_ : 0};
  }
  [[nodiscard]] std::optional<ValueKinds> At(SsaValueId value) const {
    if (value.value >= Facts().size()) return {};
    return facts_[value.value];
  }
  [[nodiscard]] std::size_t AllocatedBytes() const {
    return Facts().size_bytes();
  }
  ValueFlowWork work;

 private:
  struct LexicalIndex;
  static std::variant<RegisterValueFlow, ValueFlowFailure> Analyze(
      const abc::MethodBody& body, const RegisterSsa& ssa,
      std::uint64_t frame_size, std::span<const EntryValueFact> entries,
      const LexicalValueContext* lexicals,
      std::span<const LanguageLoadFact> loads, IdfQuota& quota,
      IdfBudget budget, std::span<const SemanticInstruction> semantics = {},
      CallResultValues calls = CallResultValues::kUnknown,
      FrameReceiver receiver = FrameReceiver::kUnknown,
      ArithmeticSemantics arithmetic = ArithmeticSemantics::kPinnedRuntime);
  RegisterValueFlow() = default;
  std::unique_ptr<ValueKinds[]> facts_;
  std::size_t count_ = 0;
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_REGISTER_VALUE_FLOW_H_
