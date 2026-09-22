#ifndef ABCD_ANALYSIS_REGISTER_VALUE_FLOW_H_
#define ABCD_ANALYSIS_REGISTER_VALUE_FLOW_H_

#include "abcd/analysis/register_ssa.h"
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
enum class ValueFlowError {
  kInvalidSsa,
  kInstruction,
  kInvalidEntryFact,
  kDuplicateEntryFact,
  kInvalidLexicals,
  kWorkLimit,
  kResourceLimit
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
      const LexicalValueContext* lexicals, IdfQuota& quota, IdfBudget budget);
  RegisterValueFlow() = default;
  std::unique_ptr<ValueKinds[]> facts_;
  std::size_t count_ = 0;
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_REGISTER_VALUE_FLOW_H_
