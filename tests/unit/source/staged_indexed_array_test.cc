#include <sstream>

#include "../../../src/analysis/value_flow.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/logical_evaluation.h"
#include "../../../src/source/tree_semantics.h"
#include "../../support/analysis/ssa_fixture.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include "abcd/source/tree_printer.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
using test::Check;
ValueProgram Graph(unsigned count, bool split, bool renamed,
                   bool wrong_edge = false, bool early_observe = false) {
  std::vector<std::uint8_t> bytes;
  const std::uint8_t array = renamed ? 1 : 0;
  const auto emit = [&](abc::Opcode op) {
    const auto n = static_cast<std::uint16_t>(op);
    bytes.push_back(static_cast<std::uint8_t>(n));
    if (n > UINT8_MAX) bytes.push_back(static_cast<std::uint8_t>(n >> 8));
  };
  emit(abc::Opcode::kCREATEEMPTYARRAY_IMM8);
  bytes.push_back(0);
  emit(abc::Opcode::kSTA_V8);
  bytes.push_back(array);
  if (split) {
    emit(abc::Opcode::kJMP_IMM8);
    bytes.push_back(2);
  }
  const auto define = [&](unsigned index) {
    emit(abc::Opcode::kSTOWNBYINDEX_IMM8_V8_IMM16);
    bytes.insert(bytes.end(), {0, array, static_cast<std::uint8_t>(index),
                               static_cast<std::uint8_t>(index >> 8)});
  };
  if (early_observe) {
    emit(abc::Opcode::kLDA_V8);
    bytes.push_back(array);
    emit(abc::Opcode::kLDOBJBYNAME_IMM8_ID16);
    bytes.insert(bytes.end(), {0, 0, 0});
  }
  for (unsigned n = 0; n < count; ++n) {
    emit(abc::Opcode::kLDA_V8);
    bytes.push_back(2);
    emit(abc::Opcode::kISTRUE);
    const auto branch = bytes.size();
    emit(abc::Opcode::kJEQZ_IMM8);
    bytes.push_back(0);
    const auto arm = [&](std::uint8_t pool) {
      emit(abc::Opcode::kLDA_STR_ID16);
      bytes.insert(bytes.end(), {pool, 0});
      if (split) {
        emit(abc::Opcode::kSTA_V8);
        bytes.push_back(1 - array);
        emit(abc::Opcode::kLDA_V8);
        bytes.push_back(1 - array);
      }
    };
    arm(0);
    if (wrong_edge) define(n);
    const auto join = bytes.size();
    emit(abc::Opcode::kJMP_IMM8);
    bytes.push_back(0);
    bytes[branch + 1] = static_cast<std::uint8_t>(bytes.size() - branch);
    arm(1);
    bytes[join + 1] = static_cast<std::uint8_t>(bytes.size() - join);
    if (!wrong_edge) define(n);
  }
  emit(abc::Opcode::kLDA_V8);
  bytes.push_back(array);
  emit(abc::Opcode::kRETURN);
  const auto body = test::Body(bytes);
  const auto method = std::get<SemanticMethod>(
      BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, 3));
  const auto proof =
      std::get<SemanticProof>(VerifySemanticSource(body, method, 2, 1));
  const auto frame = std::get<abc::FrameLayout>(
      abc::BuildFrameLayout(2, 1, {false, false, false, false}));
  auto graph =
      std::get<ValueProgram>(ImportNormalValues(body, method, proof, frame));
  Check(!std::holds_alternative<ValueSimplifyError>(
      SimplifyValues(graph, graph.revision)));
  return graph;
}
void CheckGraph(unsigned count, bool split, bool renamed) {
  auto graph = Graph(count, split, renamed);
  const auto built = BuildEvaluationPlan(graph);
  if (const auto* error = std::get_if<EvaluationFailure>(&built))
    std::cerr << "evaluation " << int(error->kind) << " obligation "
              << int(error->evaluation.obligation) << " op "
              << (error->evaluation.location.operation
                      ? int(error->evaluation.location.operation->value)
                      : -1)
              << '\n';
  Check(std::holds_alternative<EvaluationPlan>(built));
  const auto& evaluation = std::get<EvaluationPlan>(built);
  const auto& literals = *evaluation.literals;
  Check(literals.indexed_commits.size() == count &&
        literals.objects.size() == 1);
  Check(literals.objects[0].indexed_first == 0);
  Check(std::holds_alternative<std::uint64_t>(
      VerifyEvaluationPlan(graph, evaluation)));
  auto wrong = evaluation;
  ++wrong.literals->indexed_commits[0].after.offset;
  Check(std::holds_alternative<EvaluationFailure>(
      VerifyEvaluationPlan(graph, wrong)));
  wrong = evaluation;
  wrong.literals->indexed_commits[0].value.reset();
  Check(std::holds_alternative<EvaluationFailure>(
      VerifyEvaluationPlan(graph, wrong)));
  wrong = evaluation;
  wrong.literals->indexed_commits.push_back(wrong.literals->indexed_commits[0]);
  Check(std::holds_alternative<EvaluationFailure>(
      VerifyEvaluationPlan(graph, wrong)));
  wrong = evaluation;
  wrong.logical_objects[literals.objects[0].allocation.value].reset();
  Check(std::holds_alternative<EvaluationFailure>(
      VerifyEvaluationPlan(graph, wrong)));
  wrong = evaluation;
  wrong.literals->objects[0].indexed_first.reset();
  Check(std::holds_alternative<EvaluationFailure>(
      VerifyEvaluationPlan(graph, wrong)));
  auto changed = graph;
  const auto definition = literals.definitions[0];
  ++std::get<ValueIndexedWrite>(changed.ops[definition.value].operation).index;
  Check(std::holds_alternative<LiteralError>(
      VerifyLiteralPlan(changed, literals)));
  // Move the commit to an arm: the other edge no longer initializes the slot.
  changed = graph;
  OpId branch{};
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (std::holds_alternative<ValueBranch>(graph.ops[i].operation)) {
      branch = {static_cast<std::uint32_t>(i)};
      break;
    }
  changed.ops[definition.value].block =
      graph
          .edges[graph.blocks[graph.ops[branch.value].block.value]
                     .first_edge.value]
          .target;
  changed.ops[definition.value].order = 0;
  Check(std::holds_alternative<LiteralError>(
      VerifyLiteralPlan(changed, literals)));
  // Observe the unfinished array in place of the first predicate input.
  changed = graph;
  const auto allocation = literals.objects[0].allocation;
  const auto input = graph.ops[branch.value].first_use;
  changed.uses[input.value].operand = *graph.ops[allocation.value].result;
  Check(std::holds_alternative<EvaluationFailure>(
      VerifyEvaluationPlan(changed, evaluation)));
  changed = graph;
  const auto stored = std::get<ValueId>(
      changed.uses[literals.indexed_commits[0].value->value].operand);
  changed.values[stored.value].domain = ValueKinds::Unknown();
  Check(std::get<LiteralError>(VerifyLiteralPlan(changed, literals)) ==
        LiteralError::kDomain);
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  FunctionSyntax syntax{};
  syntax.callable = CallableKind::kFunction;
  syntax.strictness = Strictness::kStrict;
  syntax.names = {u"flag"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}};
  syntax.texts = {u"left", u"right"};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (const auto* string = std::get_if<ValueString>(&graph.ops[i].operation))
      constants[i] = TextId{string->string.value};
    if (const auto* write =
            std::get_if<ValueIndexedWrite>(&graph.ops[i].operation)) {
      const auto text = std::to_string(write->index);
      syntax.names.emplace_back(text.begin(), text.end());
      constants[i] =
          NameId{static_cast<std::uint32_t>(syntax.names.size() - 1)};
    }
  }
  const ExpressionConstants resolved{constants, syntax.names.size(),
                                     syntax.texts.size()};
  const auto tree =
      BuildFunctionTree(graph, control, evaluation, syntax, resolved);
  Check(std::holds_alternative<ConstructedFunction>(tree));
  const auto& function = std::get<ConstructedFunction>(tree).tree;
  Check(function.recovery_issues.empty());

  Check(std::holds_alternative<TreeSemantics>(
      VerifyTreeSemantics(graph, control, evaluation, function, resolved)));
  std::ostringstream output;
  Check(std::holds_alternative<PrintedFunction>(
      PrintFunctionTree(function, output)));
  Check(output.str().find("return [") != std::string::npos);
  auto altered = function;
  altered.expressions[altered.properties[0].value.value].expression =
      TreeLiteral{UndefinedLiteral{}};
  Check(std::holds_alternative<TreeSemanticError>(
      VerifyTreeSemantics(graph, control, evaluation, altered, resolved)));
  auto forged = function;
  forged.properties[0].value = forged.properties.back().value;
  if (count > 1)
    Check(std::holds_alternative<TreeSemanticError>(
        VerifyTreeSemantics(graph, control, evaluation, forged, resolved)));
  const auto planned = std::get<LiteralPlan>(BuildLiteralPlan(graph));
  SemanticBudget quota;
  quota.work = planned.work;
  Check(std::holds_alternative<LiteralPlan>(BuildLiteralPlan(graph, quota)));
  --quota.work;
  Check(std::get<LiteralError>(BuildLiteralPlan(graph, quota)) ==
        LiteralError::kWorkLimit);
  const auto verify =
      std::get<std::uint64_t>(VerifyLiteralPlan(graph, planned));
  quota = {};
  quota.work = verify;
  Check(std::holds_alternative<std::uint64_t>(
      VerifyLiteralPlan(graph, planned, quota)));
  --quota.work;
  Check(std::get<LiteralError>(VerifyLiteralPlan(graph, planned, quota)) ==
        LiteralError::kWorkLimit);
  std::size_t low = 0, high = SemanticBudget{}.bytes;
  while (low < high) {
    const auto mid = low + (high - low) / 2;
    quota = {};
    quota.bytes = mid;
    if (std::holds_alternative<LiteralPlan>(BuildLiteralPlan(graph, quota)))
      high = mid;
    else
      low = mid + 1;
  }
  quota = {};
  quota.bytes = low;
  Check(std::holds_alternative<LiteralPlan>(BuildLiteralPlan(graph, quota)));
  --quota.bytes;
  Check(std::get<LiteralError>(BuildLiteralPlan(graph, quota)) ==
        LiteralError::kResourceLimit);
  if (!split && !renamed)
    std::cout << "elements=" << count << " literal_work=" << planned.work
              << " verification_work=" << verify << " minimum_bytes=" << low
              << " retained_bytes=" << planned.allocated_bytes << '\n';
}
}  // namespace
int main() {
  for (const auto wrong :
       {Graph(1, false, false, true), Graph(1, false, false, false, true)}) {
    Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(wrong)));
    Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(wrong)));
  }
  for (const auto count : {1U, 200U, 201U, 400U, 800U})
    for (const auto split : {false, true})
      for (const auto renamed : {false, true})
        CheckGraph(count, split, renamed);
}
