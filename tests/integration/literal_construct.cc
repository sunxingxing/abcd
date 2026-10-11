#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_flow.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/analysis/value_edits.h"
#include "../../src/analysis/value_flow.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include "source/native_actions.h"

#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
constexpr unsigned kByteBits = 8;
abc::MethodBody Body(std::uint32_t count, bool aggregates) {
  const std::uint8_t constructor = aggregates ? 5 : 3, argument = constructor + 1;
  const auto op = [](abc::Opcode opcode) { return static_cast<std::uint8_t>(opcode); };
  std::vector<std::uint8_t> code{op(abc::Opcode::kCREATEEMPTYOBJECT), op(abc::Opcode::kSTA_V8), 0};
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto key = i + 1;
    code.insert(code.end(), {op(abc::Opcode::kLDA_V8), constructor, op(abc::Opcode::kSTA_V8), 1});
    if (aggregates) {
      code.insert(code.end(), {op(abc::Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16), 0, 0, 0,
          op(abc::Opcode::kSTA_V8), 2, op(abc::Opcode::kLDA_V8), argument,
          op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0,
          op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0, 0, 0, 2,
          op(abc::Opcode::kCREATEARRAYWITHBUFFER_IMM8_ID16), 0, 1, 0,
          op(abc::Opcode::kSTA_V8), 3, op(abc::Opcode::kCREATEEMPTYOBJECT), op(abc::Opcode::kSTA_V8), 4});
    } else code.insert(code.end(), {op(abc::Opcode::kLDA_V8), argument, op(abc::Opcode::kSTA_V8), 2});
    code.insert(code.end(), {op(abc::Opcode::kNEWOBJRANGE_IMM8_IMM8_V8), 0,
        static_cast<std::uint8_t>(aggregates ? 4 : 2), 1,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0,
        op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0,
        static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(key >> kByteBits), 0});
  }
  code.insert(code.end(), {op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kRETURN)});
  return test::Body(code);
}
void Run(std::uint32_t count, bool print, bool aggregates = false) {
  const auto body = Body(count, aggregates);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(aggregates ? 5 : 3, 2, {}));
  RouteInputIdentity identity; identity.profile = abc::ProfileId::kDynamic1206;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100}, body, frame,
      InputCaptureMode::kClosed));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto scheduled = BuildEvaluationPlan(graph);
  test::Check(std::holds_alternative<EvaluationPlan>(scheduled));
  const auto& evaluation = std::get<EvaluationPlan>(scheduled);
  test::Check(evaluation.literals && evaluation.literals->objects.size() == (aggregates ? count + 1 : 1));
  std::vector<OpId> constructors;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (graph.ops[i].life != ValueLife::kLive || !std::holds_alternative<ValueConstruct>(graph.ops[i].operation)) continue;
    constructors.push_back({static_cast<std::uint32_t>(i)});
    test::Check(evaluation.literals->nested[i].has_value());
  }
  test::Check(constructors.size() == count);
  if (count <= 2) {
    // A constructor may observe or retain its arguments. Never move an
    // unfinished literal into its own initializer, or duplicate construction.
    auto escaped = graph;
    const auto& constructor = graph.ops[constructors.front().value];
    const auto allocation = evaluation.literals->objects.front().allocation;
    for (std::uint32_t offset = 0; offset < constructor.use_count; ++offset) {
      const UseId use{constructor.first_use.value + offset};
      if (graph.uses[use.value].role != OperandRole::kArgument) continue;
      test::Check(!ReplaceUse(escaped, use, *graph.ops[allocation.value].result));
    }
    test::Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(escaped)));
    test::Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(escaped)));
    auto shared = graph;
    for (const auto& op : graph.ops) {
      if (!std::holds_alternative<ValueReturn>(op.operation)) continue;
      test::Check(!ReplaceUse(shared, op.first_use, *constructor.result));
    }
    test::Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(shared)));
    test::Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(shared)));
    if (aggregates) {
      const UseId first{constructor.first_use.value + 1}, second{constructor.first_use.value + 2};
      const auto one = std::get<ValueId>(graph.uses[first.value].operand);
      const auto two = std::get<ValueId>(graph.uses[second.value].operand);
      auto reordered = graph;
      test::Check(!ReplaceUse(reordered, first, two));
      test::Check(!ReplaceUse(reordered, second, one));
      test::Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(reordered)));
      test::Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(reordered)));
      auto forged = *evaluation.literals;
      const auto one_op = std::get<OpResult>(graph.values[one.value].origin).op;
      const auto two_op = std::get<OpResult>(graph.values[two.value].origin).op;
      forged.nested[one_op.value] = second; forged.nested[two_op.value] = first;
      test::Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(reordered, forged)));
      auto shared_argument = graph;
      for (const auto& op : graph.ops) {
        if (std::holds_alternative<ValueReturn>(op.operation))
          test::Check(!ReplaceUse(shared_argument, op.first_use, one));
      }
      test::Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(shared_argument)));
      test::Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(shared_argument)));
    }
  }
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  FunctionSyntax syntax{};
  syntax.identity = identity; syntax.method = input.Method(); syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"Ctor", u"arg", u"value"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}, {NameId{1}, NameEvidence::kDebugRange}};
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto name = std::string{"field"} + std::to_string(i);
    syntax.names.emplace_back(name.begin(), name.end());
  }
  if (aggregates) syntax.names.push_back(u"0");
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (std::holds_alternative<ValueNamedRead>(graph.ops[i].operation)) constants[i] = NameId{2};
    if (const auto* field = std::get_if<ValueOwnWrite>(&graph.ops[i].operation)) constants[i] = NameId{field->name.value + 2};
    if (const auto* allocation = std::get_if<ValueObjectAllocation>(&graph.ops[i].operation); allocation && allocation->literal)
      constants[i] = AggregateConstant{0, 1};
    if (const auto* allocation = std::get_if<ValueArrayAllocation>(&graph.ops[i].operation); allocation && allocation->literal)
      constants[i] = AggregateConstant{1, 1, AggregateKind::kArray};
  }
  const std::vector<ConstantProperty> properties = aggregates ? std::vector<ConstantProperty>{
      {NameId{2}, abcd::source::IntegerLiteral{7}}, {NameId{count + 3}, abcd::source::IntegerLiteral{9}}} :
      std::vector<ConstantProperty>{};
  const ExpressionConstants resolved{constants, syntax.names.size(), 0, {}, properties};
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(tree.recovery_issues.empty());
  const auto flow = std::get<TreeFlow>(VerifyTreeFlow(graph, control, evaluation, tree));
  const auto& verified = std::get<ConstructedFunction>(built).flow;
  test::Check(verified && verified->original);
  if (print) test::CheckNativeActions(tree, *verified->original);
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  for (const auto constructor : constructors) test::Check(flow.expressions[constructor.value].has_value());
  auto forged = evaluation;
  forged.literals->nested[constructors.front().value].reset();
  test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, forged)));
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output, {}, {}, nullptr, verified->original.get())));
  if (print) std::cout << output.str();
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2) {
    const std::string_view option{argv[1]};
    test::Check(option == "--source" || option == "--argument-source");
    Run(2, true, option == "--argument-source"); return 0;
  }
  for (const auto count : {1U, 2U, 200U, 201U, 400U, 800U}) {
    Run(count, false); Run(count, false, true);
  }
}
