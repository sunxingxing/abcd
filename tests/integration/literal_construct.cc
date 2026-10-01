#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_flow.h"
#include "../../src/analysis/value_edits.h"
#include "../../src/analysis/value_flow.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"

#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
constexpr unsigned kByteBits = 8;
constexpr std::uint32_t kLocals = 3;
constexpr std::uint8_t kConstructor = 3, kArgument = 4;
abc::MethodBody Body(std::uint32_t count) {
  const auto op = [](abc::Opcode opcode) { return static_cast<std::uint8_t>(opcode); };
  std::vector<std::uint8_t> code{op(abc::Opcode::kCREATEEMPTYOBJECT), op(abc::Opcode::kSTA_V8), 0};
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto key = i + 1;
    code.insert(code.end(), {op(abc::Opcode::kLDA_V8), kConstructor, op(abc::Opcode::kSTA_V8), 1,
        op(abc::Opcode::kLDA_V8), kArgument, op(abc::Opcode::kSTA_V8), 2,
        op(abc::Opcode::kNEWOBJRANGE_IMM8_IMM8_V8), 0, 2, 1,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0,
        op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0,
        static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(key >> kByteBits), 0});
  }
  code.insert(code.end(), {op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kRETURN)});
  return test::Body(code);
}
void Run(std::uint32_t count, bool print) {
  const auto body = Body(count);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(kLocals, 2, {}));
  RouteInputIdentity identity; identity.profile = abc::ProfileId::kDynamic1206;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100}, body, frame,
      InputCaptureMode::kClosed));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto scheduled = BuildEvaluationPlan(graph);
  test::Check(std::holds_alternative<EvaluationPlan>(scheduled));
  const auto& evaluation = std::get<EvaluationPlan>(scheduled);
  test::Check(evaluation.literals && evaluation.literals->objects.size() == 1);
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
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (std::holds_alternative<ValueNamedRead>(graph.ops[i].operation)) constants[i] = NameId{2};
    if (const auto* field = std::get_if<ValueOwnWrite>(&graph.ops[i].operation)) constants[i] = NameId{field->name.value + 2};
  }
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), {constants, count + 3});
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(tree.recovery_issues.empty());
  const auto flow = std::get<TreeFlow>(VerifyTreeFlow(graph, control, evaluation, tree));
  for (const auto constructor : constructors) test::Check(flow.expressions[constructor.value].has_value());
  auto forged = evaluation;
  forged.literals->nested[constructors.front().value].reset();
  test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, forged)));
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  if (print) std::cout << output.str();
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2) { test::Check(std::string_view(argv[1]) == "--source"); Run(2, true); return 0; }
  for (const auto count : {1U, 2U, 200U, 201U, 400U, 800U}) Run(count, false);
}
