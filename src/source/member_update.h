#ifndef ABCD_SOURCE_MEMBER_UPDATE_H_
#define ABCD_SOURCE_MEMBER_UPDATE_H_
#include "abcd/analysis/value_ir.h"
#include "abcd/source/source_tree.h"
namespace abcd::source {
struct EvaluationPlan;
class TreeCheckBudget;
enum class MemberUpdatePhase : std::uint8_t {
  kGet,
  kToNumeric,
  kOperation,
  kSet
};
struct MemberUpdatePort {
  analysis::OpId operation;
  MemberUpdatePhase phase;
  std::optional<analysis::WorkBlockId> commit;
  std::optional<std::size_t> exception;
  std::optional<analysis::ValueHandler> handler;
  bool operator==(const MemberUpdatePort&) const = default;
};
struct MemberUpdateInput {
  analysis::UseId use;
  analysis::OpId producer;
  bool moved = false;
  bool operator==(const MemberUpdateInput&) const = default;
};
struct MemberUpdate {
  analysis::OpId read, calculation, write, call, callee, first;
  analysis::UseId base;
  std::optional<analysis::UseId> key, rhs, result;
  std::optional<analysis::OpId> conversion;
  NativeMemberOperation operation;
  analysis::ValueId value;
  std::vector<MemberUpdateInput> inputs;
  std::vector<MemberUpdatePort> ports;
  bool operator==(const MemberUpdate&) const = default;
};
struct MemberUpdatePlan {
  std::size_t allocated_bytes = 0;
  std::vector<MemberUpdate> updates;
  // Intrinsic operations map to the write; inputs remain ordinary expressions.
  std::vector<std::optional<analysis::OpId>> events;
  std::vector<std::optional<analysis::OpId>> results;
  std::vector<std::optional<analysis::UseId>> moves;
  std::vector<std::uint8_t> wires;
  std::vector<std::optional<analysis::OpId>> owned;
};
enum class MemberUpdateError : std::uint8_t {
  kIdentity,
  kUse,
  kOrder,
  kCatchState,
  kResult,
  kCorrespondence,
  kStrictness
};
[[nodiscard]] const MemberUpdatePlan& MemberUpdates(const EvaluationPlan&);
[[nodiscard]] bool MemberCompoundOperator(analysis::BinaryOperator);
[[nodiscard]] MemberUpdatePlan DiscoverMemberUpdates(
    const analysis::ValueProgram&, const EvaluationPlan&, TreeCheckBudget&);
void BuildMemberUpdates(const analysis::ValueProgram&, EvaluationPlan&,
                        TreeCheckBudget&);
void VerifyMemberUpdates(const analysis::ValueProgram&, const EvaluationPlan&,
                         TreeCheckBudget&);
[[nodiscard]] const MemberUpdate* MemberUpdateAt(const EvaluationPlan&,
                                                 analysis::OpId);
[[nodiscard]] bool MemberUpdateIntrinsic(const EvaluationPlan&, analysis::OpId);
[[nodiscard]] bool MemberUpdateWire(const EvaluationPlan&, analysis::UseId);
}  // namespace abcd::source
#endif
