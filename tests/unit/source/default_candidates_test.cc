#include "../../../src/source/default_candidates.h"
#include "../../../src/source/default_value_proof.h"
#include "../../../src/source/default_body.h"
#include "../../../src/source/default_signature.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_flow.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/analysis/value_flow.h"
#include "abcd/analysis/value_environment.h"

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
// The environment remains body-owned even when native literal defaults move.
void EnvironmentPrefix() {
  for (const std::uint32_t stores : {1U, 200U, 201U, 402U}) {
    auto graph = DefaultDiamond();
    const auto prefix = stores + 1;
    for (auto& use : graph.uses)
      if (auto* op = std::get_if<OpId>(&use.owner)) op->value += prefix;
    for (auto& value : graph.values)
      if (auto* result = std::get_if<OpResult>(&value.origin)) result->op.value += prefix;
    for (auto& op : graph.ops) if (op.block.value == 0) op.order += prefix;
    graph.blocks[0].op_count += prefix;
    for (std::size_t i = 1; i < graph.blocks.size(); ++i) graph.blocks[i].first_op.value += prefix;
    const ValueId receiver{static_cast<std::uint32_t>(graph.values.size())};
    graph.values.push_back({EntryDefinition{ReceiverEntry{}}, ValueKinds::Language()});
    const ValueId environment{static_cast<std::uint32_t>(graph.values.size())};
    graph.values.push_back({OpResult{{0}}, ValueKinds::Language()});
    std::vector<ValueOp> prologue;
    const OriginId push_origin{static_cast<std::uint32_t>(graph.origins.size())};
    graph.origins.push_back({{push_origin.value}, {push_origin.value}, {}});
    prologue.push_back({ValueEnvironmentPush{1, {}}, {0}, 0, {}, 0, environment,
        LanguageEffect::kEnvironmentWrite, LanguageEffect::kNone,
        ExceptionalCompletion::kImpossible, push_origin});
    for (std::uint32_t i = 1; i <= stores; ++i) {
      const UseId use{static_cast<std::uint32_t>(graph.uses.size())};
      graph.uses.push_back({OpId{i}, OperandRole::kStoredValue, ReadPurpose::kOperand, receiver});
      const OriginId origin{static_cast<std::uint32_t>(graph.origins.size())};
      graph.origins.push_back({{origin.value}, {origin.value}, {}});
      prologue.push_back({ValueLexicalStore{{{0}, {0}}}, {0}, i, use, 1, {},
          LanguageEffect::kLexicalWrite, LanguageEffect::kNone,
          ExceptionalCompletion::kImpossible, origin});
    }
    graph.ops.insert(graph.ops.begin(), prologue.begin(), prologue.end());
    Relink(graph);
    const auto parameters = std::get<ParameterPlan>(BuildParameterPlan(graph, 1));
    const auto candidates = Find(graph);
    Check(candidates.values.size() == 1);
    auto projected = DefaultBody::Build(graph, parameters, candidates, 0);
    Check(std::holds_alternative<DefaultBody>(projected));
    const auto& body = std::get<DefaultBody>(projected);
    Check(body.Signature().Operations().size() == 3);
    for (std::uint32_t i = 0; i < prefix; ++i) {
      Check(body.Graph().ops[i].life == ValueLife::kLive);
      Check(body.Graph().ops[i].operation == graph.ops[i].operation);
      Check(body.Graph().ops[i].normal == graph.ops[i].normal);
      Check(body.Graph().ops[i].origin == graph.ops[i].origin);
    }
    Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(body.Graph())));
    graph.uses[graph.ops[1].first_use.value].operand = ValueId{0};
    Relink(graph);
    Check(std::get<DefaultProofError>(DefaultValueProof::Build(graph, parameters, candidates, 0)) ==
        DefaultProofError::kEscapedValue);
    graph.uses[graph.ops[1].first_use.value].operand = receiver;
    Relink(graph);
    std::get<ValueLexicalStore>(graph.ops[1].operation).address.depth.value = 1;
    Check(std::get<DefaultProofError>(DefaultValueProof::Build(graph, parameters, candidates, 0)) ==
        DefaultProofError::kEffect);
    graph.ops[0].operation = ValueEnvironmentPop{};
    Check(Find(graph).values.empty());
  }
}
void UnusedDefault() {
  auto graph = DefaultDiamond();
  graph.ops[2].life = ValueLife::kRemoved;
  graph.origins[2].disposition = OriginDisposition::kElidedPure;
  graph.values[2].life = ValueLife::kRemoved;
  graph.values[3].life = ValueLife::kRemoved;
  for (const auto i : {3U, 4U, 6U}) graph.uses[i].life = ValueLife::kRemoved;
  graph.uses[5].operand = SpecialValue::kUndefined;
  Relink(graph);
  const auto parameters = std::get<ParameterPlan>(BuildParameterPlan(graph, 1));
  const auto candidates = Find(graph);
  Check(candidates.values.size() == 1 && !candidates.values[0].merged);
  auto projected = DefaultBody::Build(graph, parameters, candidates, 0);
  Check(std::holds_alternative<DefaultBody>(projected));
  const auto& body = std::get<DefaultBody>(projected);
  Check(body.Signature().Operations().size() == 3);
  Check(body.Graph().origins[2].disposition == OriginDisposition::kRetained);
  Check(body.Graph().ops[2].life == ValueLife::kRemoved);
  Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(body.Graph())));
  const auto control = std::get<ControlPlan>(BuildControlPlan(body.Graph()));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(body.Graph()));
  FunctionSyntax syntax{{}, {1}, {0}, CallableKind::kMethod, Strictness::kStrict,
      {{{}, NameEvidence::kSynthesized}}, {}, {}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  auto built = BuildFunctionTree(body.Graph(), control, evaluation, std::move(syntax), {constants});
  Check(std::holds_alternative<ConstructedFunction>(built));
  auto& tree = std::get<ConstructedFunction>(built).tree;
  Check(std::holds_alternative<DefaultSignatureProof>(BuildDefaultSignature(body, tree)));
  Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(body.Graph(), control, evaluation, tree, {constants})));
  auto forged = candidates;
  forged.values[0].fallback = SpecialValue::kNull;
  Check(std::get<DefaultProofError>(DefaultValueProof::Build(graph, parameters, forged, 0)) == DefaultProofError::kLiteral);
  graph.origins[2].disposition = OriginDisposition::kRetained;
  Check(std::get<DefaultProofError>(DefaultValueProof::Build(graph, parameters, candidates, 0)) == DefaultProofError::kLiteral);
  graph.origins[2].disposition = OriginDisposition::kElidedPure;
  graph.uses[6].input = InputRead{{2}, 0};
  Check(Find(graph).values.empty()); // A propagated register copy is not a literal source.
  Check(std::get<DefaultProofError>(DefaultValueProof::Build(graph, parameters, candidates, 0)) == DefaultProofError::kLiteral);
  graph.uses[6].input.reset();
  graph.uses[5].operand = ValueId{0};
  Relink(graph);
  Check(std::get<DefaultProofError>(DefaultValueProof::Build(graph, parameters, candidates, 0)) == DefaultProofError::kEscapedValue);
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
  Check(std::holds_alternative<ValueNop>(graph.ops[1].operation));
  Check(std::holds_alternative<ValueEnvironments>(BuildValueEnvironments(graph)));
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
  for (const auto callable : {CallableKind::kFunction,CallableKind::kArrow}) {
    auto ordinary = signed_tree; ordinary.callable = callable;
    Check(std::holds_alternative<DefaultSignatureProof>(BuildDefaultSignature(owner,ordinary)));
    Check(std::holds_alternative<DefaultSignatureProof>(VerifyDefaultSignature(owner,ordinary)));
  }
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
void SnapshotDefault() {
  auto original = DefaultDiamond();
  original.arguments = ArgumentFrameFact{std::get<abc::FrameLayout>(
      abc::BuildFrameLayout(0, 1, {false, false, true, false}))};
  constexpr auto effects = LanguageEffect::kArgumentsRead | LanguageEffect::kAllocation;
  original.ops[3] = {ValueArgumentSnapshot{1}, {2}, 0, {5}, 1, ValueId{4},
      effects, effects, ExceptionalCompletion::kMayThrow, {3}};
  original.uses[5].role = OperandRole::kArgument;
  original.uses[5].purpose = ReadPurpose::kProtocol;
  original.values.push_back({OpResult{{3}}, ValueKinds::Language(), {}});
  original.ops.push_back({ValueReturn{}, {2}, 1, {7}, 1, {}, LanguageEffect::kNone,
      LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {4}});
  original.uses.push_back({OpId{4}, OperandRole::kReturnValue, ReadPurpose::kOperand, ValueId{4}});
  original.origins.push_back({{4}, {4}, {}});
  ++original.blocks[2].op_count;
  original.input.sites = {OpId{0}, OpId{1}, OpId{2}, OpId{3}, OpId{4}};
  Relink(original);
  const auto parameters = std::get<ParameterPlan>(BuildParameterPlan(original, 1));
  const auto candidates = Find(original);
  auto built = DefaultBody::Build(original, parameters, candidates, 0);
  Check(std::holds_alternative<DefaultBody>(built));
  const auto& residual = std::get<DefaultBody>(built).Graph();
  Check(residual.arguments == original.arguments && residual.input.values.empty());
  Check(!residual.input.sites[0] && !residual.input.sites[1] && !residual.input.sites[2]);
  Check(residual.input.sites[3] == OpId{3} && residual.input.sites[4] == OpId{4});
  Check(std::get<ValueId>(residual.uses[5].operand) == ValueId{0});
  Check(residual.uses[5].purpose == ReadPurpose::kProtocol && residual.ops[3].normal == effects);
  Check(std::holds_alternative<ValueEnvironments>(BuildValueEnvironments(residual)));
  const auto refuses = [&](const ValueProgram& changed) {
    Check(std::get<DefaultProofError>(DefaultValueProof::Build(changed, parameters, candidates, 0)) ==
        DefaultProofError::kEscapedValue);
  };
  auto changed = original; changed.arguments.reset(); refuses(changed);
  changed = original; changed.uses[5].operand = ValueId{0}; Relink(changed); refuses(changed);
  changed = original; changed.uses[5].role = OperandRole::kInput; refuses(changed);
  changed = original; changed.ops[3].operation = ValueCopy{}; refuses(changed);
  changed = original; ++std::get<ValueArgumentSnapshot>(changed.ops[3].operation).slots; refuses(changed);
  changed = original; ++changed.arguments->frame.parameters;
  ++std::get<ValueArgumentSnapshot>(changed.ops[3].operation).slots;
  ++changed.ops[3].use_count; refuses(changed);
}
void UndefinedProducer() {
  auto graph = DefaultDiamond();
  for (auto& value : graph.values)
    if (auto* result = std::get_if<OpResult>(&value.origin)) ++result->op.value;
  for (auto& use : graph.uses)
    if (auto* owner = std::get_if<OpId>(&use.owner)) ++owner->value;
  ++graph.ops[0].order;
  ++graph.ops[1].order;
  const ValueId undefined{static_cast<std::uint32_t>(graph.values.size())};
  const UseId input{static_cast<std::uint32_t>(graph.uses.size())};
  const OriginId origin{static_cast<std::uint32_t>(graph.origins.size())};
  graph.values.push_back({OpResult{{0}}, ValueKinds::Language(), {}});
  graph.uses.push_back({OpId{0}, OperandRole::kInput, ReadPurpose::kOperand, SpecialValue::kUndefined});
  graph.uses[1].operand = undefined;
  graph.origins.push_back({{origin.value}, {origin.value}, {}});
  graph.ops.insert(graph.ops.begin(), {ValueCopy{}, {0}, 0, input, 1, undefined,
      LanguageEffect::kNone, LanguageEffect::kNone, ExceptionalCompletion::kImpossible, origin});
  ++graph.blocks[0].op_count;
  ++graph.blocks[1].first_op.value;
  ++graph.blocks[2].first_op.value;
  Relink(graph);
  const auto parameters = std::get<ParameterPlan>(BuildParameterPlan(graph, 1));
  const auto candidates = Find(graph);
  Check(candidates.values.size() == 1 && candidates.values[0].undefined == OpId{0});
  auto projected = DefaultBody::Build(graph, parameters, candidates, 0);
  Check(std::holds_alternative<DefaultBody>(projected));
  Check(std::get<DefaultBody>(projected).Signature().Operations().size() == 4);
  graph.uses[5].operand = undefined;
  Relink(graph);
  Check(std::get<DefaultProofError>(DefaultValueProof::Build(graph, parameters, candidates, 0)) ==
        DefaultProofError::kEscapedValue);
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
void MaterializedNumber() {
  auto graph = DefaultDiamond();
  for (auto& value : graph.values)
    if (auto* result = std::get_if<OpResult>(&value.origin); result && result->op.value >= 2) ++result->op.value;
  for (auto& use : graph.uses)
    if (auto* owner = std::get_if<OpId>(&use.owner); owner && owner->value >= 2) ++owner->value;
  graph.ops[2].operation = ValueUnary{UnaryOperator::kNegate};
  graph.ops[2].order = 1;
  graph.ops[2].normal = graph.ops[2].before_throw = LanguageEffect::kUserCode;
  graph.ops[2].exceptional = ExceptionalCompletion::kMayThrow;
  graph.exceptional_exits.push_back({{3}, {2}, LanguageEffect::kUserCode});
  const ValueId number{static_cast<std::uint32_t>(graph.values.size())};
  const UseId input{static_cast<std::uint32_t>(graph.uses.size())};
  const OriginId origin{static_cast<std::uint32_t>(graph.origins.size())};
  graph.values.push_back({OpResult{{2}}, ValueKinds::Language(), {}});
  graph.uses.push_back({OpId{2}, OperandRole::kInput, ReadPurpose::kOperand, IntegerLiteral{1}});
  graph.uses[6].operand = number;
  graph.origins.push_back({{origin.value}, {origin.value}, {}});
  graph.ops.insert(graph.ops.begin() + 2, {ValueCopy{}, {1}, 0, input, 1, number,
      LanguageEffect::kNone, LanguageEffect::kNone, ExceptionalCompletion::kImpossible, origin});
  ++graph.blocks[1].op_count;
  ++graph.blocks[2].first_op.value;
  Relink(graph);
  const auto parameters = std::get<ParameterPlan>(BuildParameterPlan(graph, 1));
  auto candidates = Find(graph);
  Check(candidates.values.size() == 1 && candidates.values[0].number == OpId{2});
  Check(std::get<Float64Bits>(candidates.values[0].fallback).value == std::bit_cast<std::uint64_t>(-1.0));
  auto body = DefaultBody::Build(graph, parameters, candidates, 0);
  Check(std::holds_alternative<DefaultBody>(body));
  Check(std::get<DefaultBody>(body).Signature().Operations().size() == 4);
  Check(std::get<DefaultBody>(body).Graph().exceptional_exits.empty());
  graph.uses[input.value].operand = IntegerLiteral{0};
  candidates = Find(graph);
  Check(std::get<Float64Bits>(candidates.values[0].fallback).value == std::bit_cast<std::uint64_t>(-0.0));
  Check(std::holds_alternative<DefaultBody>(DefaultBody::Build(graph, parameters, candidates, 0)));
  graph.uses[5].operand = number;
  Relink(graph);
  Check(std::get<DefaultProofError>(DefaultValueProof::Build(graph, parameters, candidates, 0)) ==
        DefaultProofError::kEscapedValue);
}
void CapturedBoundary() {
  auto graph = DefaultDiamond();
  graph.ops[2].operation = ValueLexicalLoad{{{0},{3}}};
  graph.ops[2].use_count = 0;
  graph.ops[2].normal = LanguageEffect::kLexicalRead | LanguageEffect::kEnvironmentRead;
  graph.uses.pop_back();
  Relink(graph);
  const auto found = Find(graph);
  Check(found.values.empty() && found.capture.has_value());
  Check(found.capture->shape.initializer == OpId{2});
  Check(found.capture->address.slot.value == 3 && !found.capture->guard);
  // Discovery cannot sneak a captured read through the literal-only proof.
  const auto parameters = std::get<ParameterPlan>(BuildParameterPlan(graph,1));
  Check(std::holds_alternative<DefaultProofError>(DefaultValueProof::Build(graph,parameters,found,0)));
  const auto prove = [&](const ValueProgram& input, const DefaultCandidates& candidates) {
    const auto env = std::get<ValueEnvironments>(BuildValueEnvironments(input));
    const auto cells = std::get<CellPlan>(BuildCellPlan(input,env));
    return DefaultValueProof::BuildCaptured(input,env,cells,parameters,candidates,0);
  };
  Check(std::holds_alternative<DefaultValueProof>(prove(graph,found)));
  auto forged = found; forged.capture->address.slot.value++;
  Check(std::holds_alternative<DefaultProofError>(prove(graph,forged)));
  forged = found; forged.capture->shape.present = forged.capture->shape.missing;
  Check(std::holds_alternative<DefaultProofError>(prove(graph,forged)));
  const UseId guarded{static_cast<std::uint32_t>(graph.uses.size())};
  graph.uses.push_back({OpId{3},OperandRole::kGuardValue,ReadPurpose::kOperand,ValueId{3}});
  graph.ops.insert(graph.ops.begin()+3,{ValueGuard{}, {1},1,guarded,1,{},
      LanguageEffect::kGuard,LanguageEffect::kGuard,ExceptionalCompletion::kMayThrow,{4}});
  graph.origins.push_back({{4},{4},{}});
  graph.uses[5].owner=OpId{4}; ++graph.blocks[1].op_count; ++graph.blocks[2].first_op.value;
  Relink(graph);
  const auto checked = Find(graph);
  Check(checked.values.empty() && checked.capture && checked.capture->guard == OpId{3});
  Check(std::holds_alternative<DefaultValueProof>(prove(graph,checked)));
  const auto env = std::get<ValueEnvironments>(BuildValueEnvironments(graph));
  const auto cells = std::get<CellPlan>(BuildCellPlan(graph,env));
  auto stale = cells; ++stale.revision.value;
  Check(std::get<DefaultProofError>(DefaultValueProof::BuildCaptured(graph,env,stale,parameters,checked,0)) == DefaultProofError::kRevision);
  SemanticBudget no_work{}; no_work.work = 0;
  Check(std::get<DefaultProofError>(DefaultValueProof::BuildCaptured(graph,env,cells,parameters,checked,0,no_work)) == DefaultProofError::kWorkLimit);
  SemanticBudget no_memory{}; no_memory.bytes = 0;
  Check(std::get<DefaultProofError>(DefaultValueProof::BuildCaptured(graph,env,cells,parameters,checked,0,no_memory)) == DefaultProofError::kResourceLimit);
  forged = checked; forged.capture->guard.reset();
  Check(std::holds_alternative<DefaultProofError>(prove(graph,forged)));
  auto residual = DefaultBody::BuildCaptured(graph,env,cells,parameters,checked,0);
  Check(std::holds_alternative<DefaultBody>(residual));
  const auto& body = std::get<DefaultBody>(residual);
  Check(body.Capture() && body.Capture()->storage.slot.value == 3);
  Check(body.Graph().ops[2].life == ValueLife::kRemoved && body.Graph().ops[3].life == ValueLife::kRemoved);
  Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(body.Graph())));
  const auto residual_env = std::get<ValueEnvironments>(BuildValueEnvironments(body.Graph()));
  const auto signature_cells = std::get<CellPlan>(BuildDemandedCellPlan(body.Graph(),residual_env,{}, {},
      {&body.Capture()->storage,1}));
  Check(signature_cells.cells.size() == 1 && signature_cells.cells[0].count == 0);
  Check(signature_cells.accesses.empty() && !signature_cells.operations[2]);
  Check(std::holds_alternative<std::uint64_t>(VerifyCellPlan(body.Graph(),residual_env,signature_cells)));
  const auto signature_evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(body.Graph(), {},
      IncomingCells::kConditional, {}, {}, {}, {}, ClosureWrites::kLanguage, {}, {}, signature_cells.entry_demands));
  Check(signature_evaluation.cells && signature_evaluation.cells->cells.entry_demands.size() == 1);
  Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(body.Graph(), signature_evaluation)));
  Check(std::holds_alternative<EvaluationError>(BuildEvaluationPlan(body.Graph(), {},
      IncomingCells::kReject, {}, {}, {}, {}, ClosureWrites::kLanguage, {}, {}, signature_cells.entry_demands)));
  const auto control = std::get<ControlPlan>(BuildControlPlan(body.Graph()));
  FunctionSyntax syntax{{}, {1}, {1}, CallableKind::kFunction, Strictness::kStrict,
      {{NameId{0}, NameEvidence::kDebugRange}}, {u"value",u"outer"}, {}};
  syntax.captures.push_back({CellId{0}, {{0},{0}}, NameId{1}});
  std::vector<ExpressionConstant> constants(body.Graph().ops.size());
  auto constructed = std::get<ConstructedFunction>(BuildFunctionTree(body.Graph(),control,signature_evaluation,
      std::move(syntax),{constants,2}));
  const DefaultSignatureContext context{control,signature_evaluation};
  Check(std::holds_alternative<DefaultSignatureProof>(BuildDefaultSignature(body,constructed.tree,{},&context)));
  Check(std::holds_alternative<DefaultSignatureProof>(VerifyDefaultSignature(body,constructed.tree,{},&context)));
  Check(std::holds_alternative<DefaultSignatureError>(VerifyDefaultSignature(body,constructed.tree)));
  auto wrong = constructed.tree;
  const auto init = *wrong.parameters[0].initializer;
  wrong.expressions[init.value].expression = SymbolExpr{wrong.parameters[0].symbol};
  Check(std::holds_alternative<DefaultSignatureError>(VerifyDefaultSignature(body,wrong,{},&context)));
  wrong = constructed.tree;
  wrong.names[wrong.symbols[wrong.parameters[0].symbol.value].name.value] = u"outer";
  Check(std::holds_alternative<DefaultSignatureError>(VerifyDefaultSignature(body,wrong,{},&context)));
  auto lost = signature_cells; lost.entry_demands.clear();
  Check(std::holds_alternative<CellError>(VerifyCellPlan(body.Graph(),residual_env,lost)));
  lost = signature_cells; ++lost.entry_demands[0].slot.value;
  Check(std::holds_alternative<CellError>(VerifyCellPlan(body.Graph(),residual_env,lost)));
  auto unused = graph;
  unused.values[2].life = ValueLife::kRemoved;
  unused.uses[3].life = unused.uses[4].life = ValueLife::kRemoved;
  unused.uses[5].operand = IntegerLiteral{99}; Relink(unused);
  const auto unused_candidate = Find(unused);
  Check(unused_candidate.capture && !unused_candidate.capture->shape.merged);
  Check(std::holds_alternative<DefaultValueProof>(prove(unused,unused_candidate)));
  auto omitted_guard = unused_candidate; omitted_guard.capture->guard.reset();
  Check(std::holds_alternative<DefaultProofError>(prove(unused,omitted_guard)));
  const auto unused_env = std::get<ValueEnvironments>(BuildValueEnvironments(unused));
  const auto unused_cells = std::get<CellPlan>(BuildCellPlan(unused,unused_env));
  const auto unused_body = DefaultBody::BuildCaptured(unused,unused_env,unused_cells,parameters,unused_candidate,0);
  Check(std::holds_alternative<DefaultBody>(unused_body));
  Check(std::get<DefaultBody>(unused_body).Signature().Operations().size() == 4);
  Check(std::get<DefaultBody>(unused_body).Capture()->candidate.guard == OpId{3});
  auto escaped = graph;
  escaped.uses[5].operand = ValueId{0}; Relink(escaped);
  Check(std::holds_alternative<DefaultProofError>(prove(escaped,checked)));
  graph.uses[guarded.value].operand=ValueId{0}; Relink(graph);
  Check(!Find(graph).capture);
  Check(std::holds_alternative<DefaultProofError>(prove(graph,checked)));
  graph.uses[guarded.value].operand=ValueId{3}; Relink(graph);
  graph.ops[3].operation=ValueDebugger{}; graph.ops[3].normal=LanguageEffect::kDebugger;
  Check(!Find(graph).capture);
}
}  // namespace
int main() { CapturedBoundary(); UnusedDefault(); EnvironmentPrefix(); MatchAndBoundaries(); RejectCoincidences(); NegatedLiteral(); ProofAndEscapes(); BodyProjection(); SnapshotDefault(); UndefinedProducer(); MaterializedNumber(); }
