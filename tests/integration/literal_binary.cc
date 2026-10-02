#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/instruction_sources.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include "../../src/analysis/value_edits.h"
#include "../../src/analysis/value_flow.h"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
constexpr unsigned kByteBits = 8;
enum class BinaryCase { kStringAdd, kEqual, kLess, kBitOr, kUnknownAdd };
std::uint64_t Run(abc::ProfileId profile, std::uint32_t count, bool print = false, BinaryCase kind = BinaryCase::kStringAdd, bool barrier = false) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kCREATEEMPTYOBJECT), op(abc::Opcode::kSTA_V8), 0};
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto key = i + 2;
    bytes.insert(bytes.end(), {op(abc::Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16), 0, 0, 0,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0, op(abc::Opcode::kSTA_V8), 1});
    if (kind == BinaryCase::kStringAdd) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_STR_ID16), 0, 0});
    else bytes.insert(bytes.end(), {op(abc::Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16), 0, 0, 0,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0});
    auto binary = abc::Opcode::kADD2_IMM8_V8;
    switch (kind) {
      case BinaryCase::kStringAdd: case BinaryCase::kUnknownAdd: break;
      case BinaryCase::kEqual: binary = abc::Opcode::kSTRICTEQ_IMM8_V8; break;
      case BinaryCase::kLess: binary = abc::Opcode::kLESS_IMM8_V8; break;
      case BinaryCase::kBitOr: binary = abc::Opcode::kOR2_IMM8_V8; break;
    }
    bytes.insert(bytes.end(), {op(binary), 0, 1});
    if (barrier) bytes.push_back(op(abc::Opcode::kDEBUGGER));
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0, static_cast<std::uint8_t>(key),
        static_cast<std::uint8_t>(key >> kByteBits), 0});
  }
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kRETURN)});
  const auto body = test::Body(bytes);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(2, 0, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto prepared = FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed);
  if (kind == BinaryCase::kUnknownAdd) {
    test::Check(!std::holds_alternative<FunctionInput>(prepared)); return 0;
  }
  const auto& input = std::get<FunctionInput>(prepared);
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto planned = BuildEvaluationPlan(graph);
  if (barrier) {
    test::Check(std::holds_alternative<EvaluationError>(planned)); return 0;
  }
  test::Check(std::holds_alternative<EvaluationPlan>(planned));
  const auto& evaluation = std::get<EvaluationPlan>(planned);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(graph, evaluation)));
  if (count == 2) {
    Run(profile, 1, false, kind, true);
    const auto binary = std::find_if(graph.ops.begin(), graph.ops.end(), [](const auto& op) {
      return std::holds_alternative<ValueBinary>(op.operation);
    });
    test::Check(binary != graph.ops.end());
    auto shared = graph;
    for (const auto& op : graph.ops) if (std::holds_alternative<ValueReturn>(op.operation))
      test::Check(!ReplaceUse(shared, op.first_use, *binary->result));
    test::Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(shared)));
    test::Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(shared)));
    auto forged = evaluation;
    forged.literals->nested[static_cast<std::size_t>(binary - graph.ops.begin())].reset();
    test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, forged)));
  }
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  FunctionSyntax syntax{};
  syntax.identity = identity; syntax.method = input.Method(); syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"Lookup", u"left"}; syntax.texts = {u"suffix"};
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto name = std::string{"field"} + std::to_string(i);
    syntax.names.emplace_back(name.begin(), name.end());
  }
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (std::holds_alternative<ValueGlobalRead>(graph.ops[i].operation)) constants[i] = NameId{0};
    if (std::holds_alternative<ValueNamedRead>(graph.ops[i].operation)) constants[i] = NameId{1};
    if (std::holds_alternative<ValueString>(graph.ops[i].operation)) constants[i] = TextId{0};
    if (const auto* field = std::get_if<ValueOwnWrite>(&graph.ops[i].operation)) constants[i] = NameId{field->name.value};
    if (std::holds_alternative<ValueBinary>(graph.ops[i].operation))
      test::Check(evaluation.operations[i].kind == EvaluationKind::kNestedLiteral);
  }
  const ExpressionConstants resolved{constants, syntax.names.size(), syntax.texts.size()};
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(input, graph, tree)));
  const GlobalScope globals{}; std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output, {}, {}, &globals)));
  if (print) std::cout << output.str();
  return evaluation.work;
}
}
int main(int argc, char** argv) {
  if (argc == 2) {
    const auto option = std::string_view(argv[1]);
    test::Check(option == "--source" || option == "--equal-source" || option == "--less-source" || option == "--bit-source");
    const auto kind = option == "--equal-source" ? BinaryCase::kEqual : option == "--less-source" ?
        BinaryCase::kLess : option == "--bit-source" ? BinaryCase::kBitOr : BinaryCase::kStringAdd;
    Run(abc::ProfileId::kDynamic1206, 2, true, kind); return 0;
  }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    for (const auto kind : {BinaryCase::kEqual, BinaryCase::kLess, BinaryCase::kBitOr, BinaryCase::kUnknownAdd})
      Run(profile, 2, false, kind);
    std::uint64_t previous = 0;
    for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
      const auto work = Run(profile, count);
      if (count == 402) test::Check(work < 3 * previous);
      previous = work;
    }
  }
}
