#include "../../../src/analysis/value_flow.h"
#include "../../../src/analysis/value_edits.h"

#include <cstdlib>

namespace {
using namespace abcd::analysis;
void Check(bool condition) {
  if (!condition) std::abort();
}
void Append(ValueProgram& graph, OperandUse use) {
  const UseId id{static_cast<std::uint32_t>(graph.uses.size())};
  auto& value = graph.values[std::get<ValueId>(use.operand).value];
  use.next = value.first_use;
  if (use.next) graph.uses[use.next->value].previous = id;
  value.first_use = id;
  graph.uses.push_back(use);
}
ValueProgram ParallelEdges() {
  ValueProgram graph;
  graph.blocks = {{{0}, 1, {0}, 2}, {{1}, 1, {2}, 0}};
  graph.edges = {{{0}, {1}, FlowEdgeKind::kTaken, 0},
                 {{0}, {1}, FlowEdgeKind::kFallthrough, 1}};
  graph.values.push_back({EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}});
  graph.values.push_back({PhiDefinition{{1}}, ValueKinds::Language(), {}});
  graph.ops.push_back({ValueBranch{UnaryOperator::kToBoolean}, {0}, 0, {0}, 1, {},
      LanguageEffect::kNone, LanguageEffect::kNone,
      ExceptionalCompletion::kImpossible, {0}});
  graph.ops.push_back({ValueReturn{}, {1}, 0, {1}, 1, {},
      LanguageEffect::kNone, LanguageEffect::kNone,
      ExceptionalCompletion::kImpossible, {1}});
  Append(graph, {OpId{0}, OperandRole::kCondition, ReadPurpose::kOperand,
      ValueId{0}, {}, {}, {}, {}});
  Append(graph, {OpId{1}, OperandRole::kReturnValue, ReadPurpose::kOperand,
      ValueId{1}, {}, {}, {}, {}});
  Append(graph, {ValueId{1}, OperandRole::kPhiInput, ReadPurpose::kOperand,
      ValueId{0}, EdgeId{0}, {}, {}, {}});
  Append(graph, {ValueId{1}, OperandRole::kPhiInput, ReadPurpose::kOperand,
      ValueId{0}, EdgeId{1}, {}, {}, {}});
  return graph;
}
}  // namespace

int main() {
  auto graph = ParallelEdges();
  auto verified = VerifyValueFlow(graph);
  const auto* proof = std::get_if<ValueFlowProof>(&verified);
  Check(proof && proof->revision == graph.revision && proof->work != 0);
  graph.uses[3].incoming = EdgeId{0};
  Check(std::get<ValueAvailabilityError>(VerifyValueFlow(graph)) == ValueAvailabilityError::kInvalidPhi);
  graph.uses[3].incoming = EdgeId{1};
  Check(!ReplaceUse(graph, UseId{0}, ValueId{1}));
  Check(std::get<ValueAvailabilityError>(VerifyValueFlow(graph)) == ValueAvailabilityError::kUnavailableValue);
  Check(!ReplaceUse(graph, UseId{0}, ValueId{0}));
  graph.edges[1].kind = FlowEdgeKind::kTaken;
  Check(std::get<ValueAvailabilityError>(VerifyValueFlow(graph)) == ValueAvailabilityError::kInvalidTerminator);
  graph.edges[1].kind = FlowEdgeKind::kResume;
  Check(std::get<ValueAvailabilityError>(VerifyValueFlow(graph)) == ValueAvailabilityError::kResumeFlow);
  graph = ParallelEdges();
  graph.values.push_back({PhiDefinition{{1}}, ValueKinds::Language(), {}});
  Check(std::get<ValueAvailabilityError>(VerifyValueFlow(graph)) == ValueAvailabilityError::kInvalidPhi);
  graph = ParallelEdges();
  graph.edges[0].target = {0};
  graph.values[1].origin = PhiDefinition{{0}};
  graph.uses[2].incoming = MethodEntryKey{};
  graph.uses[3].incoming = EdgeId{0};
  Check(!ReplaceUse(graph, UseId{3}, ValueId{1}));
  Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(graph)));
  // The real backedge may carry the header phi; the external entry may not.
  Check(!ReplaceUse(graph, UseId{2}, ValueId{1}));
  Check(std::get<ValueAvailabilityError>(VerifyValueFlow(graph)) ==
        ValueAvailabilityError::kUnavailableValue);
  graph = ParallelEdges();
  graph.edges[0].target = {0};
  graph.edges[1].target = {0};
  Check(std::get<ValueAvailabilityError>(VerifyValueFlow(graph)) == ValueAvailabilityError::kUnreachableBlock);
  graph = ParallelEdges();
  SemanticBudget budget;
  budget.work = 0;
  Check(std::get<ValueAvailabilityError>(VerifyValueFlow(graph, budget)) == ValueAvailabilityError::kWorkLimit);
  budget = {};
  budget.bytes = 0;
  Check(std::get<ValueAvailabilityError>(VerifyValueFlow(graph, budget)) == ValueAvailabilityError::kResourceLimit);
}
