#include "../../../src/source/default_candidates.h"
#include "../../../src/source/default_value_proof.h"
#include "../../../src/source/default_body.h"
#include "../../../src/source/default_signature.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_flow.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/analysis/value_flow.h"

#include <cstdlib>
#include <bit>
#include <type_traits>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
using abcd::analysis::IntegerLiteral;
static_assert(!std::is_default_constructible_v<DefaultValueProof>);
static_assert(!std::is_copy_constructible_v<DefaultValueProof>);
static_assert(!std::is_default_constructible_v<DefaultBody>);
static_assert(!std::is_copy_constructible_v<DefaultBody>);
void Check(bool condition) { if (!condition) std::abort(); }
ValueProgram DefaultDiamond() {
  ValueProgram graph;
  graph.values = {
      {EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), UseId{0}},
      {OpResult{{0}}, ValueKinds::Language(), UseId{2}},
      {PhiDefinition{{2}}, ValueKinds::Language(), UseId{5}},
      {OpResult{{2}}, ValueKinds::Language(), UseId{4}}};
  graph.uses = {
      {OpId{0}, OperandRole::kLeft, ReadPurpose::kOperand, ValueId{0}, {}, {}, {}, UseId{3}},
      {OpId{0}, OperandRole::kRight, ReadPurpose::kOperand, SpecialValue::kUndefined},
      {OpId{1}, OperandRole::kCondition, ReadPurpose::kOperand, ValueId{1}},
      {ValueId{2}, OperandRole::kPhiInput, ReadPurpose::kOperand, ValueId{0},
       IncomingKey{EdgeId{0}}, {}, UseId{0}},
      {ValueId{2}, OperandRole::kPhiInput, ReadPurpose::kOperand, ValueId{3}, IncomingKey{EdgeId{2}}},
      {OpId{3}, OperandRole::kReturnValue, ReadPurpose::kOperand, ValueId{2}},
      {OpId{2}, OperandRole::kInput, ReadPurpose::kOperand, IntegerLiteral{-1}}};
  graph.ops = {
      {ValueBinary{BinaryOperator::kStrictNotEqual}, {0}, 0, {0}, 2, ValueId{1},
       LanguageEffect::kNone, LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {0}},
      {ValueBranch{UnaryOperator::kTrueOrNonzeroNumber}, {0}, 1, {2}, 1, {},
       LanguageEffect::kNone, LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {1}},
      {ValueCopy{}, {1}, 0, {6}, 1, ValueId{3},
       LanguageEffect::kNone, LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {2}},
      {ValueReturn{}, {2}, 0, {5}, 1, {},
       LanguageEffect::kNone, LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {3}}};
  graph.blocks = {{{0}, 2, {0}, 2}, {{2}, 1, {2}, 1}, {{3}, 1, {3}, 0}};
  graph.edges = {{{0}, {2}, FlowEdgeKind::kTaken, 0},
                 {{0}, {1}, FlowEdgeKind::kFallthrough, 1},
                 {{1}, {2}, FlowEdgeKind::kFallthrough, 0}};
  for (std::uint32_t i = 0; i < graph.ops.size(); ++i)
    graph.origins.push_back({{i}, {i}, {}});
  return graph;
}
DefaultCandidates Find(const ValueProgram& graph) {
  const auto parameters = std::get<ParameterPlan>(BuildParameterPlan(graph, 1));
  return std::get<DefaultCandidates>(FindDefaultCandidates(graph, parameters, 0));
}
void Relink(ValueProgram& graph) {
  for (auto& value : graph.values) value.first_use.reset();
  std::vector<std::optional<UseId>> last(graph.values.size());
  for (std::size_t i = 0; i < graph.uses.size(); ++i) {
    auto& use = graph.uses[i];
    use.previous.reset(); use.next.reset();
    const auto* value = std::get_if<ValueId>(&use.operand);
    if (!value || use.life != ValueLife::kLive) continue;
    const UseId id{static_cast<std::uint32_t>(i)};
    use.previous = last[value->value];
    if (use.previous) graph.uses[use.previous->value].next = id;
    else graph.values[value->value].first_use = id;
    last[value->value] = id;
  }
  Check(!VerifyValueUses(graph, 1024 * 1024));
}
void ProofAndEscapes() {
  auto graph = DefaultDiamond();
  const auto parameters = std::get<ParameterPlan>(BuildParameterPlan(graph, 1));
  auto candidates = Find(graph);
  auto proved = DefaultValueProof::Build(graph, parameters, candidates, 0);
  Check(std::holds_alternative<DefaultValueProof>(proved));
  const auto& proof = std::get<DefaultValueProof>(proved);
  Check(proof.BodyEntry() == WorkBlockId{2} && proof.Defaults().size() == 1);
  Check(proof.Operations().size() == 3);
  Check(std::get<DefaultProofError>(DefaultValueProof::Build(graph, parameters, candidates, 0,
        {1024 * 1024, proof.Work() - 1})) == DefaultProofError::kWorkLimit);
  candidates.values[0].fallback = IntegerLiteral{0};
  Check(std::get<DefaultProofError>(DefaultValueProof::Build(graph, parameters, candidates, 0)) == DefaultProofError::kLiteral);
  candidates = Find(graph);
  graph.uses[5].operand = ValueId{0};
  Relink(graph);
  Check(std::get<DefaultProofError>(DefaultValueProof::Build(graph, parameters, candidates, 0)) == DefaultProofError::kEscapedValue);
  graph.uses[5].operand = ValueId{1};
  Relink(graph);
  Check(std::get<DefaultProofError>(DefaultValueProof::Build(graph, parameters, candidates, 0)) == DefaultProofError::kEscapedValue);
  graph = DefaultDiamond();
  candidates.values[0].comparison = OpId{1};
  Check(std::holds_alternative<DefaultProofError>(DefaultValueProof::Build(graph, parameters, candidates, 0)));
  candidates = Find(graph);
  ++graph.cfg_revision.value;
  Check(std::get<DefaultProofError>(DefaultValueProof::Build(graph, parameters, candidates, 0)) == DefaultProofError::kRevision);
}
void BodyProjection() {
  const auto original = DefaultDiamond();
  const auto parameters = std::get<ParameterPlan>(BuildParameterPlan(original, 1));
  const auto candidates = Find(original);
  auto projected = DefaultBody::Build(original, parameters, candidates, 0);
  Check(std::holds_alternative<DefaultBody>(projected));
  const auto& owner = std::get<DefaultBody>(projected);
  const auto& graph = owner.Graph();
  Check(graph.revision.value == original.revision.value + 1);
  Check(graph.cfg_revision.value == original.cfg_revision.value + 1);
  Check(original.ops[0].life == ValueLife::kLive && original.blocks[0].edge_count == 2);
  Check(graph.ops[0].life == ValueLife::kRemoved && graph.ops[2].life == ValueLife::kRemoved);
  Check(graph.blocks[0].edge_count == 1 && graph.edges[0].kind == FlowEdgeKind::kFallthrough);
  Check(graph.values[2].life == ValueLife::kRemoved && graph.values[0].life == ValueLife::kLive);
  Check(std::get<ValueId>(graph.uses[5].operand) == ValueId{0});
  Check(graph.origins[0].disposition == OriginDisposition::kRetained);
  Check(graph.input.sites.empty() && graph.input.values.empty());
  Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(graph)));
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  std::vector<ExpressionConstant> constants(graph.ops.size());
  FunctionSyntax syntax{{}, {1}, {0}, CallableKind::kMethod, Strictness::kStrict, {{{}, NameEvidence::kSynthesized}}, {}, {}};
  auto tree = BuildFunctionTree(graph, control, evaluation, std::move(syntax), {constants, 0, 0, {}});
  Check(std::holds_alternative<ConstructedFunction>(tree));
  Check(std::get<ConstructedFunction>(tree).tree.symbols.size() == 1);
  auto& syntax_tree = std::get<ConstructedFunction>(tree).tree;
  Check(syntax_tree.statements[syntax_tree.body.value].origin == TreeOriginId{3});
  syntax_tree.statements[syntax_tree.body.value].origin = TreeOriginId{0};
  Check(std::get<TreeFlowError>(VerifyTreeFlow(graph, control, evaluation, syntax_tree)) ==
        TreeFlowError::kPlacement);
  syntax_tree.statements[syntax_tree.body.value].origin = TreeOriginId{3};
  auto signed_tree = syntax_tree;
  const auto signature = BuildDefaultSignature(owner, signed_tree);
  Check(std::holds_alternative<DefaultSignatureProof>(signature));
  Check(signed_tree.signature.size() == 3 && signed_tree.parameters[0].initializer.has_value());
  const auto initializer = *signed_tree.parameters[0].initializer;
  Check(std::get<source::IntegerLiteral>(std::get<TreeLiteral>(
      signed_tree.expressions[initializer.value].expression)).value == -1);
  Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(
      graph, control, evaluation, signed_tree, {constants, 0, 0, {}})));
  const auto verified = std::get<DefaultSignatureProof>(VerifyDefaultSignature(owner, signed_tree));
  Check(std::get<DefaultSignatureError>(VerifyDefaultSignature(owner, signed_tree,
      {1024 * 1024, verified.work - 1})) == DefaultSignatureError::kWorkLimit);
  Check(std::get<DefaultSignatureError>(VerifyDefaultSignature(owner, signed_tree, {0, 0})) ==
        DefaultSignatureError::kResourceLimit);
  auto invalid = signed_tree;
  invalid.expressions[initializer.value].expression = TreeLiteral{source::IntegerLiteral{0}};
  Check(std::get<DefaultSignatureError>(VerifyDefaultSignature(owner, invalid)) == DefaultSignatureError::kLiteral);
  invalid = signed_tree;
  invalid.signature.pop_back();
  Check(std::get<DefaultSignatureError>(VerifyDefaultSignature(owner, invalid)) == DefaultSignatureError::kPlacement);
  invalid = signed_tree;
  invalid.signature[0].origin = TreeOriginId{2};
  Check(std::get<DefaultSignatureError>(VerifyDefaultSignature(owner, invalid)) == DefaultSignatureError::kOrigin);
  invalid = signed_tree;
  invalid.statements[invalid.body.value].origin = TreeOriginId{0};
  Check(std::get<DefaultSignatureError>(VerifyDefaultSignature(owner, invalid)) == DefaultSignatureError::kOrigin);
  invalid = signed_tree;
  invalid.signature[1].operation = invalid.signature[0].operation;
  Check(std::get<DefaultSignatureError>(VerifyDefaultSignature(owner, invalid)) == DefaultSignatureError::kStructure);
  invalid = signed_tree;
  ++invalid.cfg_revision.value;
  Check(std::get<DefaultSignatureError>(VerifyDefaultSignature(owner, invalid)) == DefaultSignatureError::kRevision);
  Check(std::get<DefaultBodyError>(DefaultBody::Build(original, parameters, candidates, 0, {0, 0})) == DefaultBodyError::kResourceLimit);
  Check(std::get<DefaultBodyError>(DefaultBody::Build(original, parameters, candidates, 0,
        {1024 * 1024, owner.Work() - 1})) == DefaultBodyError::kWorkLimit);
  auto changed = original;
  ++changed.cfg_revision.value;
  Check(std::get<DefaultBodyError>(DefaultBody::Build(changed, parameters, candidates, 0)) == DefaultBodyError::kRevision);
  changed = original;
  changed.uses[5].operand = ValueId{0};
  Relink(changed);
  Check(std::get<DefaultBodyError>(DefaultBody::Build(changed, parameters, candidates, 0)) == DefaultBodyError::kProof);
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
  graph.uses[6].operand = SpecialValue::kNaN;
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
  graph.ops[2].operation = ValueUnary{UnaryOperator::kNegate};
  graph.ops[2].normal = graph.ops[2].before_throw = LanguageEffect::kUserCode;
  graph.ops[2].exceptional = ExceptionalCompletion::kMayThrow;
  graph.exceptional_exits.push_back({{2}, {2}, LanguageEffect::kUserCode});
  graph.uses[6].operand = IntegerLiteral{1};
  Check(!VerifyValueUses(graph, 1024 * 1024));
  auto found = Find(graph);
  Check(found.values.size() == 1 && found.values[0].initializer == OpId{2});
  const auto parameters = std::get<ParameterPlan>(BuildParameterPlan(graph, 1));
  Check(std::holds_alternative<DefaultValueProof>(DefaultValueProof::Build(graph, parameters, found, 0)));
  Check(std::get<Float64Bits>(found.values[0].fallback).value == std::bit_cast<std::uint64_t>(-1.0));
  graph.uses[6].operand = IntegerLiteral{0};
  found = Find(graph);
  Check(std::get<Float64Bits>(found.values[0].fallback).value == std::bit_cast<std::uint64_t>(-0.0));
  Check(std::holds_alternative<DefaultValueProof>(DefaultValueProof::Build(graph, parameters, found, 0)));
  graph.uses[5].operand = ValueId{3};
  Relink(graph);
  Check(std::get<DefaultProofError>(DefaultValueProof::Build(graph, parameters, found, 0)) == DefaultProofError::kEscapedValue);
  graph.uses[5].operand = ValueId{2};
  graph.uses[6].operand = ValueId{0};
  Relink(graph);
  Check(Find(graph).values.empty()); // No speculative coercion of a parameter.
}
}  // namespace
int main() { MatchAndBoundaries(); RejectCoincidences(); NegatedLiteral(); ProofAndEscapes(); BodyProjection(); }
