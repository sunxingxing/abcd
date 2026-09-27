#include "../../../src/analysis/value_flow.h"
#include "../../../src/analysis/value_edits.h"
#include "../../../src/analysis/value_rewrite.h"
#include "abcd/analysis/value_simplify.h"

#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd::analysis;
void Append(ValueProgram& graph, OperandUse use) {
  const UseId id{static_cast<std::uint32_t>(graph.uses.size())};
  auto& value = graph.values[std::get<ValueId>(use.operand).value];
  use.next = value.first_use;
  if (use.next) graph.uses[use.next->value].previous = id;
  value.first_use = id;
  graph.uses.push_back(use);
}
ValueProgram ReadAndCatch() {
  ValueProgram graph;
  // Before/Commit pairs for property read, normal return, handler return;
  // then the independent Catch definition. PCs are original instructions.
  graph.blocks = {{{0}, 1, {0}, 2}, {{1}, 0, {2}, 1},
      {{1}, 1, {3}, 1}, {{2}, 0, {4}, 0},
      {{2}, 1, {4}, 1}, {{3}, 0, {5}, 0}, {{3}, 0, {5}, 1}};
  graph.phases = {{{0}, {0}, ExceptionPhase::kBefore, OpId{0}},
      {{1}, {0}, ExceptionPhase::kCommit, OpId{0}},
      {{2}, {1}, ExceptionPhase::kBefore, OpId{1}},
      {{3}, {1}, ExceptionPhase::kCommit, OpId{1}},
      {{4}, {2}, ExceptionPhase::kBefore, OpId{2}},
      {{5}, {2}, ExceptionPhase::kCommit, OpId{2}},
      {{6}, {2}, ExceptionPhase::kCatch, {}}};
  graph.edges = {{{0}, {1}, FlowEdgeKind::kFallthrough, 0},
      {{0}, {6}, FlowEdgeKind::kException, 1},
      {{1}, {2}, FlowEdgeKind::kFallthrough, 0},
      {{2}, {3}, FlowEdgeKind::kFallthrough, 0},
      {{4}, {5}, FlowEdgeKind::kFallthrough, 0},
      {{6}, {4}, FlowEdgeKind::kFallthrough, 0}};
  graph.origins = {{{0}, {0}, {}}, {{1}, {3}, {}}, {{2}, {4}, {}}};
  graph.values = {{EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}},
      {OpResult{{0}, WorkBlockId{1}}, ValueKinds::Language(), {}},
      {CatchDefinition{{6}}, ValueKinds::Language(), {}}};
  constexpr auto kReadEffects = LanguageEffect::kHeapRead | LanguageEffect::kUserCode;
  graph.ops = {{ValueNamedRead{{0}}, {0}, 0, {0}, 1, ValueId{1},
      kReadEffects, kReadEffects, ExceptionalCompletion::kMayThrow, {0}},
      {ValueReturn{}, {2}, 0, {1}, 1, {}, LanguageEffect::kNone,
      LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {1}},
      {ValueReturn{}, {4}, 0, {2}, 1, {}, LanguageEffect::kNone,
      LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {2}}};
  Append(graph, {OpId{0}, OperandRole::kBase, ReadPurpose::kOperand,
      ValueId{0}, {}, {}, {}, {}});
  Append(graph, {OpId{1}, OperandRole::kReturnValue, ReadPurpose::kOperand,
      ValueId{1}, {}, {}, {}, {}});
  Append(graph, {OpId{2}, OperandRole::kReturnValue, ReadPurpose::kOperand,
      ValueId{2}, {}, {}, {}, {}});
  return graph;
}
ValueProgram LinearPhases(std::uint32_t count) {
  ValueProgram graph;
  for (std::uint32_t i = 0; i < count; ++i) {
    const WorkBlockId before{2 * i}, commit{before.value + 1};
    const OpId op{i};
    const bool returns = i + 1 == count;
    graph.phases.push_back({{before.value}, {i}, ExceptionPhase::kBefore, op});
    graph.phases.push_back({{commit.value}, {i}, ExceptionPhase::kCommit, op});
    graph.blocks.push_back({op, 1, {static_cast<std::uint32_t>(graph.edges.size())}, 1});
    graph.edges.push_back({before, commit, FlowEdgeKind::kFallthrough, 0});
    graph.blocks.push_back({{i + 1}, 0, {static_cast<std::uint32_t>(graph.edges.size())},
        static_cast<std::uint8_t>(returns ? 0 : 1)});
    if (!returns) graph.edges.push_back({commit, {commit.value + 1}, FlowEdgeKind::kFallthrough, 0});
    graph.origins.push_back({{i}, {i}, {}});
    graph.ops.push_back({returns ? ValueOperation{ValueReturn{}} : ValueOperation{ValueNop{}},
        before, 0, {0}, 0, {}, LanguageEffect::kNone, LanguageEffect::kNone,
        ExceptionalCompletion::kImpossible, {i}});
  }
  return graph;
}
ValueProgram HandlerJoin() {
  auto graph = ReadAndCatch();
  graph.blocks = {{{0}, 1, {0}, 2}, {{1}, 0, {2}, 1},
      {{1}, 1, {3}, 1}, {{2}, 0, {4}, 0}, {{2}, 0, {4}, 1}};
  graph.phases.resize(5);
  graph.phases[4] = {{4}, {1}, ExceptionPhase::kCatch, {}};
  graph.edges = {{{0}, {1}, FlowEdgeKind::kFallthrough, 0},
      {{0}, {4}, FlowEdgeKind::kException, 1},
      {{1}, {2}, FlowEdgeKind::kFallthrough, 0},
      {{2}, {3}, FlowEdgeKind::kFallthrough, 0},
      {{4}, {2}, FlowEdgeKind::kFallthrough, 0}};
  graph.ops.pop_back();
  graph.origins.pop_back();
  graph.values[2].origin = CatchDefinition{{4}};
  graph.values.push_back({PhiDefinition{{2}}, ValueKinds::Language(), {}});
  graph.uses.clear();
  for (auto& value : graph.values) value.first_use.reset();
  Append(graph, {OpId{0}, OperandRole::kBase, ReadPurpose::kOperand,
      ValueId{0}, {}, {}, {}, {}});
  Append(graph, {OpId{1}, OperandRole::kReturnValue, ReadPurpose::kOperand,
      ValueId{3}, {}, {}, {}, {}});
  Append(graph, {ValueId{3}, OperandRole::kPhiInput, ReadPurpose::kOperand,
      ValueId{1}, IncomingKey{EdgeId{2}}, {}, {}, {}});
  Append(graph, {ValueId{3}, OperandRole::kPhiInput, ReadPurpose::kOperand,
      ValueId{2}, IncomingKey{EdgeId{4}}, {}, {}, {}});
  return graph;
}
}  // namespace

int main() {
  using abcd::test::Check;
  auto graph = ReadAndCatch();
  Check(!VerifyValueUses(graph, graph.uses.size()));
  const auto checked = VerifyPhasedValueFlow(graph);
  Check(std::holds_alternative<PhasedValueFlowProof>(checked));
  Check(std::holds_alternative<ValueAvailabilityError>(VerifyValueFlow(graph)));
  Check(std::get<ValueSimplifyError>(SimplifyValues(graph, graph.revision)) ==
      ValueSimplifyError::kUnsupportedFlow);
  Check(std::get<ValueRewriteError>(VerifyValueRewrite(graph, graph, {})) ==
      ValueRewriteError::kEffect);
  Check(!ReplaceUse(graph, {2}, ValueId{1}));
  Check(std::get<ValueAvailabilityError>(VerifyPhasedValueFlow(graph)) ==
      ValueAvailabilityError::kUnavailableValue);
  graph = ReadAndCatch();
  Check(!ReplaceUse(graph, {1}, ValueId{2}));
  Check(std::get<ValueAvailabilityError>(VerifyPhasedValueFlow(graph)) ==
      ValueAvailabilityError::kUnavailableValue);
  graph = ReadAndCatch();
  // A handler phi may receive an old value from Before, never the read's
  // normal result. Incoming availability is checked on the actual throw edge.
  graph.values.push_back({PhiDefinition{{6}}, ValueKinds::Language(), {}});
  Append(graph, {ValueId{3}, OperandRole::kPhiInput, ReadPurpose::kOperand,
      ValueId{0}, IncomingKey{EdgeId{1}}, {}, {}, {}});
  Check(!ReplaceUse(graph, {2}, ValueId{3}));
  Check(std::holds_alternative<PhasedValueFlowProof>(VerifyPhasedValueFlow(graph)));
  Check(!ReplaceUse(graph, {3}, ValueId{1}));
  Check(std::get<ValueAvailabilityError>(VerifyPhasedValueFlow(graph)) ==
      ValueAvailabilityError::kUnavailableValue);
  graph = ReadAndCatch();
  std::get<OpResult>(graph.values[1].origin).commit = WorkBlockId{0};
  Check(VerifyValueUses(graph, graph.uses.size()).has_value());
  graph = ReadAndCatch();
  graph.phases.clear();
  Check(VerifyValueUses(graph, graph.uses.size()).has_value());
  graph = ReadAndCatch();
  graph.edges[1].kind = FlowEdgeKind::kFallthrough;
  Check(std::holds_alternative<ValueAvailabilityError>(VerifyPhasedValueFlow(graph)));
  graph = ReadAndCatch();
  graph.phases[6].instruction = {0};
  Check(std::holds_alternative<ValueAvailabilityError>(VerifyPhasedValueFlow(graph)));
  graph = ReadAndCatch();
  graph.edges[0].target = {6};
  Check(std::holds_alternative<ValueAvailabilityError>(VerifyPhasedValueFlow(graph)));
  graph = ReadAndCatch();
  graph.ops[0].exceptional = ExceptionalCompletion::kImpossible;
  Check(std::holds_alternative<ValueAvailabilityError>(VerifyPhasedValueFlow(graph)));
  graph = ReadAndCatch();
  const auto work = std::get<PhasedValueFlowProof>(checked).work;
  Check(std::holds_alternative<PhasedValueFlowProof>(
      VerifyPhasedValueFlow(graph, {SemanticBudget{}.bytes, work})));
  Check(std::get<ValueAvailabilityError>(VerifyPhasedValueFlow(
      graph, {SemanticBudget{}.bytes, work - 1})) == ValueAvailabilityError::kWorkLimit);
  Check(std::get<ValueAvailabilityError>(VerifyPhasedValueFlow(graph, {0})) ==
      ValueAvailabilityError::kResourceLimit);
  graph = HandlerJoin();
  Check(std::holds_alternative<PhasedValueFlowProof>(VerifyPhasedValueFlow(graph)));
  // Normal entry to handler code bypasses Catch; only the join phi can select
  // between normal and exceptional results without using an unavailable value.
  Check(!ReplaceUse(graph, {1}, ValueId{2}));
  Check(std::get<ValueAvailabilityError>(VerifyPhasedValueFlow(graph)) ==
      ValueAvailabilityError::kUnavailableValue);
  graph = HandlerJoin();
  Check(!ReplaceUse(graph, {3}, ValueId{1}));
  Check(std::get<ValueAvailabilityError>(VerifyPhasedValueFlow(graph)) ==
      ValueAvailabilityError::kUnavailableValue);
  for (const std::uint32_t count : {200U, 201U, 402U}) {
    auto linear = LinearPhases(count);
    const auto result = VerifyPhasedValueFlow(linear);
    Check(std::holds_alternative<PhasedValueFlowProof>(result));
    const auto steps = std::get<PhasedValueFlowProof>(result).work;
    Check(std::holds_alternative<PhasedValueFlowProof>(VerifyPhasedValueFlow(
        linear, {SemanticBudget{}.bytes, steps})));
    Check(std::get<ValueAvailabilityError>(VerifyPhasedValueFlow(
        linear, {SemanticBudget{}.bytes, steps - 1})) == ValueAvailabilityError::kWorkLimit);
    linear.ops.back().operation = ValueJump{};
    linear.blocks.back().edge_count = 1;
    linear.edges.push_back({{static_cast<std::uint32_t>(linear.blocks.size() - 1)},
        {0}, FlowEdgeKind::kTaken, 0});
    Check(std::holds_alternative<PhasedValueFlowProof>(VerifyPhasedValueFlow(linear)));
  }
}
