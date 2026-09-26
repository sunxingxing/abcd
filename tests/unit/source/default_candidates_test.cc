#include "../../../src/source/default_candidates.h"

#include <cstdlib>
#include <bit>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
void Check(bool condition) { if (!condition) std::abort(); }
ValueProgram DefaultDiamond() {
  ValueProgram graph;
  graph.values = {
      {EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), UseId{0}},
      {OpResult{{0}}, ValueKinds::Language(), UseId{2}},
      {PhiDefinition{{2}}, ValueKinds::Language(), UseId{5}}};
  graph.uses = {
      {OpId{0}, OperandRole::kLeft, ReadPurpose::kOperand, ValueId{0}, {}, {}, {}, UseId{3}},
      {OpId{0}, OperandRole::kRight, ReadPurpose::kOperand, SpecialValue::kUndefined},
      {OpId{1}, OperandRole::kCondition, ReadPurpose::kOperand, ValueId{1}},
      {ValueId{2}, OperandRole::kPhiInput, ReadPurpose::kOperand, ValueId{0},
       IncomingKey{EdgeId{0}}, {}, UseId{0}},
      {ValueId{2}, OperandRole::kPhiInput, ReadPurpose::kOperand, IntegerLiteral{-1}, IncomingKey{EdgeId{2}}},
      {OpId{2}, OperandRole::kReturnValue, ReadPurpose::kOperand, ValueId{2}}};
  graph.ops = {
      {ValueBinary{BinaryOperator::kStrictNotEqual}, {0}, 0, {0}, 2, ValueId{1},
       LanguageEffect::kNone, LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {0}},
      {ValueBranch{UnaryOperator::kTrueOrNonzeroNumber}, {0}, 1, {2}, 1, {},
       LanguageEffect::kNone, LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {1}},
      {ValueReturn{}, {2}, 0, {5}, 1, {},
       LanguageEffect::kNone, LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {2}}};
  graph.blocks = {{{0}, 2, {0}, 2}, {{2}, 0, {2}, 1}, {{2}, 1, {3}, 0}};
  graph.edges = {{{0}, {2}, FlowEdgeKind::kTaken, 0},
                 {{0}, {1}, FlowEdgeKind::kFallthrough, 1},
                 {{1}, {2}, FlowEdgeKind::kFallthrough, 0}};
  return graph;
}
DefaultCandidates Find(const ValueProgram& graph) {
  const auto parameters = std::get<ParameterPlan>(BuildParameterPlan(graph, 1));
  return std::get<DefaultCandidates>(FindDefaultCandidates(graph, parameters, 0));
}
void MatchAndBoundaries() {
  auto graph = DefaultDiamond();
  Check(!VerifyValueUses(graph, 1024 * 1024));
  const auto found = Find(graph);
  Check(found.values.size() == 1);
  const auto& candidate = found.values.front();
  Check(candidate.parameter == 0 && candidate.entry == ValueId{0} && candidate.merged == ValueId{2});
  Check(candidate.header == WorkBlockId{0} && candidate.arm == WorkBlockId{1} && candidate.join == WorkBlockId{2});
  Check(std::get<IntegerLiteral>(candidate.fallback).value == -1);
  const auto parameters = std::get<ParameterPlan>(BuildParameterPlan(graph, 1));
  Check(std::get<DefaultCandidates>(FindDefaultCandidates(graph, parameters, 1)).values.empty());
  Check(std::get<DefaultCandidateError>(FindDefaultCandidates(graph, parameters, 0, {0, 0})) ==
        DefaultCandidateError::kResourceLimit);
  Check(std::get<DefaultCandidateError>(FindDefaultCandidates(graph, parameters, 0,
        {1024 * 1024, found.work - 1})) == DefaultCandidateError::kWorkLimit);
  ++graph.revision.value;
  Check(std::get<DefaultCandidateError>(FindDefaultCandidates(graph, parameters, 0)) == DefaultCandidateError::kRevision);
}
void RejectCoincidences() {
  auto graph = DefaultDiamond();
  std::get<ValueBinary>(graph.ops[0].operation).op = BinaryOperator::kStrictEqual;
  Check(Find(graph).values.empty()); // Polarity changed without changing edges.
  std::get<ValueBranch>(graph.ops[1].operation).predicate = UnaryOperator::kFalseOrZero;
  Check(Find(graph).values.size() == 1);
  graph = DefaultDiamond();
  graph.uses[4].incoming = IncomingKey{EdgeId{1}};
  Check(Find(graph).values.empty()); // Missing path must arrive on the arm's edge.
  graph = DefaultDiamond();
  graph.uses[4].operand = SpecialValue::kHole;
  Check(Find(graph).values.empty());
  graph = DefaultDiamond();
  graph.edges.push_back({{2}, {1}, FlowEdgeKind::kTaken, 0});
  Check(Find(graph).values.empty()); // A foreign entry into the default arm.
  graph = DefaultDiamond();
  graph.edges.push_back({{2}, {0}, FlowEdgeKind::kTaken, 0});
  Check(Find(graph).values.empty()); // A body loop back to the apparent prologue.
  graph = DefaultDiamond();
  graph.uses[0].operand = IntegerLiteral{0};
  Check(Find(graph).values.empty()); // A non-parameter undefined comparison.
}
void NegatedLiteral() {
  auto graph = DefaultDiamond();
  graph.ops.insert(graph.ops.begin() + 2,
      {ValueUnary{UnaryOperator::kNegate}, {1}, 0, {6}, 1, ValueId{3},
       LanguageEffect::kUserCode, LanguageEffect::kUserCode, ExceptionalCompletion::kMayThrow, {3}});
  graph.blocks[1].op_count = 1;
  graph.blocks[2].first_op = OpId{3};
  graph.uses[5].owner = OpId{3};
  graph.uses.push_back({OpId{2}, OperandRole::kInput, ReadPurpose::kOperand, IntegerLiteral{1}});
  graph.values.push_back({OpResult{{2}}, ValueKinds::Language(), UseId{4}});
  graph.uses[4].operand = ValueId{3};
  Check(!VerifyValueUses(graph, 1024 * 1024));
  auto found = Find(graph);
  Check(found.values.size() == 1 && found.values[0].initializer == OpId{2});
  Check(std::get<Float64Bits>(found.values[0].fallback).bits == std::bit_cast<std::uint64_t>(-1.0));
  graph.uses[6].operand = IntegerLiteral{0};
  found = Find(graph);
  Check(std::get<Float64Bits>(found.values[0].fallback).bits == std::bit_cast<std::uint64_t>(-0.0));
  graph.uses[6].operand = ValueId{0};
  Check(Find(graph).values.empty()); // No speculative coercion of a parameter.
}
}  // namespace
int main() { MatchAndBoundaries(); RejectCoincidences(); NegatedLiteral(); }
