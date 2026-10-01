#include "abcd/analysis/value_simplify.h"
#include "../../../src/analysis/value_edits.h"
#include "../../../src/analysis/value_rewrite.h"

#include <array>
#include <cstdlib>
#include <limits>

namespace {
using namespace abcd::analysis;
void Check(bool condition) {
  if (!condition) std::abort();
}

void AppendUse(ValueProgram& graph, OpId owner, ValueId operand, OperandRole role) {
  const UseId id{static_cast<std::uint32_t>(graph.uses.size())};
  auto& value = graph.values[operand.value];
  graph.uses.push_back({owner, role, ReadPurpose::kOperand, operand, {}, {},
                        {}, value.first_use});
  if (value.first_use) graph.uses[value.first_use->value].previous = id;
  value.first_use = id;
}

ValueProgram CopyChain(std::uint32_t length) {
  ValueProgram graph;
  graph.values.push_back({EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}});
  for (std::uint32_t i = 0; i < length; ++i) {
    graph.origins.push_back({{i}, {i}, {}});
    graph.values.push_back({OpResult{OpId{i}}, ValueKinds::Language(), {}});
    graph.ops.push_back({ValueCopy{}, {0}, i, {i}, 1, ValueId{i + 1},
        LanguageEffect::kNone, LanguageEffect::kNone,
        ExceptionalCompletion::kImpossible, {i}});
    AppendUse(graph, OpId{i}, ValueId{i}, OperandRole::kInput);
  }
  graph.origins.push_back({{length}, {length}, {}});
  graph.ops.push_back({ValueReturn{}, {0}, length, {length}, 1, {},
      LanguageEffect::kNone, LanguageEffect::kNone,
      ExceptionalCompletion::kImpossible, {length}});
  AppendUse(graph, OpId{length}, ValueId{length}, OperandRole::kReturnValue);
  graph.blocks.push_back({{0}, length + 1, {0}, 0});
  return graph;
}

void ChainsAndRevision() {
  constexpr std::array<std::uint32_t, 4> kChainLengths{1, 200, 201, 4096};
  for (const auto length : kChainLengths) {
    auto graph = CopyChain(length);
    Check(!VerifyValueUses(graph, graph.uses.size()));
    auto result = SimplifyValues(graph, IrRevision{0});
    const auto* stats = std::get_if<ValueSimplifyStats>(&result);
    Check(stats && stats->removed_ops == length && stats->replaced_uses == length);
    Check(graph.revision == IrRevision{1} && graph.cfg_revision == CfgRevision{0});
    Check(std::get<ValueId>(graph.uses[length].operand) == ValueId{0});
    Check(!VerifyValueUses(graph, graph.uses.size()));
    result = SimplifyValues(graph, IrRevision{0});
    Check(std::get<ValueSimplifyError>(result) == ValueSimplifyError::kStaleRevision);
    result = SimplifyValues(graph, IrRevision{1});
    stats = std::get_if<ValueSimplifyStats>(&result);
    Check(stats && stats->removed_ops == 0 && stats->replaced_uses == 0);
    Check(graph.revision == IrRevision{1});
  }
}

void JumpCycles() {
  for (const std::uint32_t length : {1U, 2U, 16U, 200U, 201U}) {
    ValueProgram graph;
    for (std::uint32_t i = 0; i <= length; ++i) {
      graph.blocks.push_back({{i}, 1, {i}, 1});
      graph.ops.push_back({ValueJump{}, {i}, 0, {0}, 0, {}, LanguageEffect::kNone,
          LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {i}});
      graph.origins.push_back({{i}, {i}, {}});
      graph.edges.push_back({{i}, {i == length ? 1U : i + 1}, FlowEdgeKind::kTaken, 0});
    }
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
      const auto result = SimplifyValues(graph, graph.revision);
      const auto* stats = std::get_if<ValueSimplifyStats>(&result);
      Check(stats && stats->specialized_edges == 0 && graph.blocks.size() == length + 1);
    }
  }
}

void LiteralRoots() {
  const std::array<ValueOperand, 5> literals{IntegerLiteral{-7}, SpecialValue::kNull,
      SpecialValue::kUndefined, Float64Bits{UINT64_C(0x8000000000000000)},
      Float64Bits{UINT64_C(0x7ff8000000000042)}};
  for (const auto length : {1U, 200U, 201U, 4096U}) for (const auto& literal : literals) {
    auto before = CopyChain(length);
    Check(!ReplaceLiteralUse(before, UseId{0}, literal));
    auto after = before;
    const auto simplified = SimplifyValues(after, after.revision);
    const auto* stats = std::get_if<ValueSimplifyStats>(&simplified);
    Check(stats && stats->removed_ops == length);
    Check(after.uses[length].operand.index() == literal.index());
    std::visit([&](const auto& expected) {
      using T = std::decay_t<decltype(expected)>;
      const auto& actual = std::get<T>(after.uses[length].operand);
      if constexpr (std::is_same_v<T, SpecialValue>) Check(actual == expected);
      else Check(actual.value == expected.value);
    }, literal);
    Check(!VerifyValueUses(after, after.uses.size()));
    after.revision = before.revision;
    std::vector<ValueId> roots(length + 1, ValueId{1}); roots[0] = {0};
    Check(std::holds_alternative<ValueRewriteProof>(VerifyValueRewrite(before, after, roots)));
    after.uses[length].operand = IntegerLiteral{99};
    Check(std::holds_alternative<ValueRewriteError>(VerifyValueRewrite(before, after, roots)));
  }
  auto effectful = CopyChain(2);
  Check(!ReplaceLiteralUse(effectful, UseId{0}, IntegerLiteral{7}));
  effectful.ops[0].normal = LanguageEffect::kHeapWrite;
  const auto result = SimplifyValues(effectful, effectful.revision);
  Check(std::holds_alternative<ValueSimplifyStats>(result));
  Check(effectful.ops[0].life == ValueLife::kLive &&
      std::holds_alternative<ValueId>(effectful.uses.back().operand));
}

void FailureRollback() {
  constexpr std::uint32_t kLength = 201;
  auto graph = CopyChain(kLength);
  const auto original = std::get<ValueId>(graph.uses[kLength].operand);
  const auto check_original = [&] {
    Check(graph.revision == IrRevision{0});
    Check(std::get<ValueId>(graph.uses[kLength].operand) == original);
    for (const auto& op : graph.ops) Check(op.life == ValueLife::kLive);
    Check(!VerifyValueUses(graph, graph.uses.size()));
  };
  auto result = SimplifyValues(graph, IrRevision{0}, {0, kDefaultSemanticWork});
  Check(std::get<ValueSimplifyError>(result) == ValueSimplifyError::kResourceLimit);
  check_original();
  SemanticBudget budget;
  budget.work = 0;
  result = SimplifyValues(graph, IrRevision{0}, budget);
  Check(std::get<ValueSimplifyError>(result) == ValueSimplifyError::kWorkLimit);
  check_original();
  graph.revision.value = std::numeric_limits<std::uint64_t>::max();
  result = SimplifyValues(graph, graph.revision);
  Check(std::get<ValueSimplifyError>(result) == ValueSimplifyError::kRevisionLimit);
  graph.revision.value = 0;
  check_original();
  Check(!ReplaceUse(graph, UseId{0}, ValueId{kLength}));
  result = SimplifyValues(graph, graph.revision);
  // A copy-only cycle violates ordinary SSA availability before aliasing starts.
  Check(std::get<ValueSimplifyError>(result) == ValueSimplifyError::kInvalidGraph);
  Check(graph.revision == IrRevision{0});
}

void KeepObservable() {
  auto graph = CopyChain(1);
  // A getter with an unused result still executes. Its copy input is replaced,
  // but the operation, exception edge and original PC are never elided.
  graph.values.push_back({OpResult{OpId{1}}, ValueKinds::Language(), {}});
  graph.ops[1] = {ValueNamedRead{{0}}, {0}, 1, {1}, 1, ValueId{2},
      LanguageEffect::kHeapRead | LanguageEffect::kUserCode,
      LanguageEffect::kHeapRead, ExceptionalCompletion::kMayThrow, {1}};
  graph.uses[1].role = OperandRole::kBase;
  graph.origins.push_back({{2}, {2}, {}});
  graph.ops.push_back({ValueReturn{}, {0}, 2, {2}, 1, {},
      LanguageEffect::kNone, LanguageEffect::kNone,
      ExceptionalCompletion::kImpossible, {2}});
  AppendUse(graph, OpId{2}, ValueId{1}, OperandRole::kReturnValue);
  graph.blocks[0].op_count = 3;
  graph.exceptional_exits.push_back({{1}, {1}, LanguageEffect::kHeapRead});
  auto changed = graph;
  changed.exceptional_exits.front().handler = ValueHandler{{{0}, {0}, {0}}, {0}};
  const std::array<ValueId, 3> identities{ValueId{0}, ValueId{1}, ValueId{2}};
  Check(std::get<ValueRewriteError>(VerifyValueRewrite(graph, changed, identities)) ==
      ValueRewriteError::kEffect);
  const auto result = SimplifyValues(graph, IrRevision{0});
  const auto* stats = std::get_if<ValueSimplifyStats>(&result);
  Check(stats && stats->removed_ops == 1 && stats->replaced_uses == 2);
  Check(graph.ops[1].life == ValueLife::kLive);
  Check(graph.origins[1].disposition == OriginDisposition::kRetained);
  Check(graph.exceptional_exits.size() == 1);
  Check(!VerifyValueUses(graph, graph.uses.size()));
}

void DeadPhiCycle() {
  ValueProgram graph;
  graph.blocks.push_back({{0}, 1, {0}, 1});
  graph.edges.push_back({{0}, {0}, FlowEdgeKind::kTaken, 0});
  graph.origins.push_back({{0}, {0}, {}});
  graph.ops.push_back({ValueJump{}, {0}, 0, {0}, 0, {},
      LanguageEffect::kNone, LanguageEffect::kNone,
      ExceptionalCompletion::kImpossible, {0}});
  graph.values.push_back({PhiDefinition{{0}}, ValueKinds::Language(), UseId{1}});
  graph.values.push_back({EntryDefinition{ParameterEntry{0}},
                         ValueKinds::Language(), UseId{0}});
  graph.uses.push_back({ValueId{0}, OperandRole::kPhiInput, ReadPurpose::kOperand,
      ValueId{1}, MethodEntryKey{}, {}, {}, {}});
  graph.uses.push_back({ValueId{0}, OperandRole::kPhiInput, ReadPurpose::kOperand,
      ValueId{0}, EdgeId{0}, {}, {}, {}});
  Check(!VerifyValueUses(graph, graph.uses.size()));
  const auto result = SimplifyValues(graph, IrRevision{0});
  const auto* stats = std::get_if<ValueSimplifyStats>(&result);
  Check(stats && stats->removed_phis == 1 && stats->removed_ops == 0);
  Check(graph.values[0].life == ValueLife::kRemoved);
  Check(graph.values[1].life == ValueLife::kLive);
  Check(graph.ops[0].life == ValueLife::kLive);
  Check(!VerifyValueUses(graph, graph.uses.size()));
}

void RewriteCertificate() {
  const auto before = CopyChain(2);
  auto after = before;
  Check(std::holds_alternative<ValueSimplifyStats>(SimplifyValues(after, {})));
  // The checker consumes an uncommitted candidate; publication increments the
  // revision only after checking. A stale committed revision must be rejected.
  const std::array<ValueId, 3> roots{ValueId{0}, ValueId{0}, ValueId{0}};
  Check(std::get<ValueRewriteError>(VerifyValueRewrite(before, after, roots)) ==
        ValueRewriteError::kRevision);
  after.revision = before.revision;
  Check(std::holds_alternative<ValueRewriteProof>(VerifyValueRewrite(before, after, roots)));
  after.origins[0].pc.value += 1;
  Check(std::get<ValueRewriteError>(VerifyValueRewrite(before, after, roots)) ==
        ValueRewriteError::kOrigin);
  after.origins[0].pc = before.origins[0].pc;
  after.ops[2].normal = LanguageEffect::kHeapRead;
  Check(std::get<ValueRewriteError>(VerifyValueRewrite(before, after, roots)) ==
        ValueRewriteError::kEffect);
  after.ops[2].normal = LanguageEffect::kNone;
  auto invented = roots;
  invented[0] = ValueId{1};
  Check(std::get<ValueRewriteError>(VerifyValueRewrite(before, after, invented)) ==
        ValueRewriteError::kAlias);
  after.uses[2].role = OperandRole::kCondition;
  Check(std::get<ValueRewriteError>(VerifyValueRewrite(before, after, roots)) ==
        ValueRewriteError::kOperand);
}
}  // namespace

int main() {
  JumpCycles();
  LiteralRoots();
  ChainsAndRevision();
  FailureRollback();
  KeepObservable();
  DeadPhiCycle();
  RewriteCertificate();
}
