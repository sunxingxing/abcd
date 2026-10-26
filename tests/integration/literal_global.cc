#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_flow.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/instruction_sources.h"
#include "../../src/source/tree_globals.h"
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
constexpr unsigned kByteBits = 8; // ISA ID16 is two little-endian bytes.
abc::MethodBody Body(std::uint32_t count, bool buffered, bool direct, bool barrier = false) {
  const auto op = [](abc::Opcode opcode) { return static_cast<std::uint8_t>(opcode); };
  std::vector<std::uint8_t> code{op(abc::Opcode::kCREATEEMPTYOBJECT), op(abc::Opcode::kSTA_V8), 0};
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto key = i + 2;
    if (buffered) code.insert(code.end(), {op(abc::Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16), 0, 0, 0});
    else code.push_back(op(abc::Opcode::kCREATEEMPTYOBJECT));
    code.insert(code.end(), {op(abc::Opcode::kSTA_V8), 1, op(abc::Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16), 0, 0, 0});
    if (!direct) code.insert(code.end(), {op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0});
    if (barrier && i == 0) code.push_back(op(abc::Opcode::kDEBUGGER));
    code.insert(code.end(), {op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0, 1, 0, 1,
        op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0,
        static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(key >> kByteBits), 0});
  }
  code.insert(code.end(), {op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kRETURN)});
  return test::Body(code);
}
std::uint64_t Run(abc::ProfileId profile, std::uint32_t count, bool buffered = false, bool direct = false, bool print = false) {
  const auto body = Body(count, buffered, direct);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(2, 0, {}));
  RouteInputIdentity identity; identity.profile = profile;
  identity.contract = SourceContract::kLanguageAndHostV3;  // Pinned analysis fixture.
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100}, body, frame,
      InputCaptureMode::kClosed));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto scheduled = BuildEvaluationPlan(graph);
  test::Check(std::holds_alternative<EvaluationPlan>(scheduled));
  const auto& evaluation = std::get<EvaluationPlan>(scheduled);
  test::Check(evaluation.literals && evaluation.literals->objects.size() == count + 1);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(graph, evaluation)));
  std::vector<OpId> global_reads;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (!std::holds_alternative<ValueGlobalRead>(graph.ops[i].operation)) continue;
    global_reads.push_back({static_cast<std::uint32_t>(i)});
    test::Check(evaluation.operations[i].kind == EvaluationKind::kNestedLiteral);
  }
  test::Check(global_reads.size() == count);
  if (count == 2) {
    const auto blocked_body = Body(count, buffered, direct, true);
    const auto blocked_input = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100}, blocked_body, frame,
        InputCaptureMode::kClosed));
    const auto blocked_values = std::get<FunctionValues>(BuildFunctionValues(blocked_input, blocked_body, frame));
    test::Check(std::get<EvaluationFailure>(BuildEvaluationPlan(blocked_values.graph)) == EvaluationError::kUnsupportedOperation);
    auto unsupported = graph;
    std::get<ValueGlobalRead>(unsupported.ops[global_reads.front().value].operation).lookup = GlobalLookup::kVar;
    test::Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(unsupported, {}, {}, {}, {}, nullptr, nullptr, SourceContract::kLanguageSourceV8)));
    auto shared = graph;
    for (const auto& op : graph.ops) if (std::holds_alternative<ValueReturn>(op.operation))
      test::Check(!ReplaceUse(shared, op.first_use, *graph.ops[global_reads.front().value].result));
    test::Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(shared)));
    test::Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(shared)));
    auto forged = evaluation;
    forged.literals->nested[global_reads.front().value].reset();
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, forged)));
  }
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  FunctionSyntax syntax{}; syntax.identity.contract = SourceContract::kLanguageAndHostV3;
  syntax.identity = identity; syntax.method = input.Method(); syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"Lookup", u"value"};
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto name = std::string{"field"} + std::to_string(i);
    syntax.names.emplace_back(name.begin(), name.end());
  }
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (std::holds_alternative<ValueGlobalRead>(graph.ops[i].operation)) constants[i] = NameId{0};
    if (std::holds_alternative<ValueNamedRead>(graph.ops[i].operation)) constants[i] = NameId{1};
    if (const auto* field = std::get_if<ValueOwnWrite>(&graph.ops[i].operation)) constants[i] = NameId{field->name.value};
    if (const auto* allocation = std::get_if<ValueObjectAllocation>(&graph.ops[i].operation); allocation && allocation->literal)
      constants[i] = AggregateConstant{0, 1};
  }
  const std::vector<ConstantProperty> properties{{NameId{1}, NullLiteral{}}};
  const ExpressionConstants resolved{constants, syntax.names.size(), 0, {}, properties};
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(tree.recovery_issues.empty());
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(input, graph, tree)));
  GlobalScope globals{};
  const std::vector<std::string_view> shadowed{"Lookup"};
  const GlobalScope collision{shadowed};
  test::Check(std::holds_alternative<TreeGlobals>(VerifyTreeGlobals(tree, &globals)));
  test::Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &collision)) == TreeGlobalError::kShadowed);
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output, {}, {}, &globals)));
  if (print) std::cout << output.str();
  return evaluation.literals->work;
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2) {
    const std::string_view option{argv[1]};
    test::Check(option == "--source" || option == "--buffer-source" || option == "--direct-source");
    Run(abc::ProfileId::kDynamic1206, 2, option == "--buffer-source", option == "--direct-source", true);
    return 0;
  }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    for (const bool buffered : {false, true}) for (const bool direct : {false, true}) {
      std::uint64_t previous = 0;
      for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
        const auto work = Run(profile, count, buffered, direct);
        if (count > 201) test::Check(work < 3 * previous);
        previous = work;
      }
    }
  }
}
