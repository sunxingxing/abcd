#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/call_plan.h"
#include "../../src/source/instruction_sources.h"
#include "analysis/ssa_fixture.h"
#include "abcd/source/tree_printer.h"

#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
constexpr unsigned kByteBits = 8;
enum class Selection { kConditional, kAnd, kOr };
abc::MethodBody Body(unsigned count, bool direct, bool barrier, Selection selection) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr auto kGuard = abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16;
  std::vector<std::uint8_t> bytes;
  for (unsigned i = 0; i < count; ++i) {
    // Retrieve the member before selecting its argument. One arm owns the
    // original read/guard pair; the other keeps the left value or a constant.
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 3,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0, op(abc::Opcode::kSTA_V8), 0,
        op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0,
        op(abc::Opcode::kSTA_V8), 2});
    if (selection == Selection::kConditional)
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_STR_ID16), 0, 0,
          op(abc::Opcode::kSTRICTEQ_IMM8_V8), 0, 2});
    else bytes.push_back(op(abc::Opcode::kISTRUE));
    const auto branch = bytes.size();
    bytes.insert(bytes.end(), {op(selection == Selection::kOr ? abc::Opcode::kJNEZ_IMM8 :
        abc::Opcode::kJEQZ_IMM8), 0});
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDEXTERNALMODULEVAR_IMM8), 0,
        op(kGuard), static_cast<std::uint8_t>(static_cast<std::uint16_t>(kGuard) >> kByteBits), 3, 0});
    if (!direct) bytes.insert(bytes.end(), {op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 2, 0});
    if (barrier) bytes.push_back(op(abc::Opcode::kDEBUGGER));
    const auto jump = bytes.size(); bytes.insert(bytes.end(), {op(abc::Opcode::kJMP_IMM8), 0});
    bytes[branch + 1] = static_cast<std::uint8_t>(bytes.size() - branch);
    if (selection == Selection::kConditional)
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDAI_IMM32), 0xc0, 1, 0, 0}); // 448, the independent fallback.
    else bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 2});
    bytes[jump + 1] = static_cast<std::uint8_t>(bytes.size() - jump);
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 1, op(abc::Opcode::kLDA_V8), 0,
        op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, 3, 1});
  }
  bytes.push_back(op(abc::Opcode::kRETURN));
  return test::Body(bytes);
}
void Run(unsigned count, bool direct, bool print, bool barrier = false,
    Selection selection = Selection::kConditional) {
  const auto body = Body(count, direct, barrier, selection);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(3, 1, {}));
  RouteInputIdentity identity; identity.profile = abc::ProfileId::kDynamic1206;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100}, body, frame,
      InputCaptureMode::kClosed));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  std::vector<ModuleEvaluation> modules;
  for (const auto& pair : values.modules.pairs)
    modules.push_back({pair, std::get<ValueModuleRead>(graph.ops[pair.read.value].operation), {{7}, {0}}});
  test::Check(modules.size() == count);
  const auto planned = BuildEvaluationPlan(graph, {}, IncomingCells::kReject, {}, modules);
  test::Check(std::holds_alternative<EvaluationPlan>(planned));
  const auto& evaluation = std::get<EvaluationPlan>(planned);
  const auto calls = BuildCallPlan(graph, evaluation);
  if (barrier) { test::Check(std::get<CallPlanError>(calls) == CallPlanError::kReference); return; }
  test::Check(std::holds_alternative<CallPlan>(calls));
  unsigned logical = 0;
  for (const auto& placement : evaluation.operations) logical += placement.kind == EvaluationKind::kLogicalValue;
  test::Check(logical == count);
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  FunctionSyntax syntax{};
  syntax.identity = identity; syntax.method = input.Method(); syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"value", u"method", u"probe", u"option", u"imported"}; syntax.texts = {u"on"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}};
  syntax.modules = {{{{7}, {0}}, NameId{4}, ModuleAccess::kReadOnly, NameEvidence::kMetadata}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (const auto* read = std::get_if<ValueNamedRead>(&graph.ops[i].operation)) constants[i] = NameId{read->name.value + 1};
    if (std::holds_alternative<ValueString>(graph.ops[i].operation)) constants[i] = TextId{0};
  }
  const ExpressionConstants resolved{constants, 5, 1};
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(tree.recovery_issues.empty());
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  const auto flow = std::get<TreeFlow>(VerifyTreeFlow(graph, control, evaluation, tree));
  test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(input, graph, tree)));
  for (const auto& module : modules) {
    const auto read = module.pair.read.value, guard = module.pair.guard->value;
    test::Check(flow.expressions[read] && flow.expressions[read] == flow.expressions[guard]);
  }
  if (count <= 2) {
    auto forged = evaluation; forged.modules.front().pair.guard.reset();
    test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, forged)));
    forged = evaluation; forged.operations[modules.front().pair.guard->value].kind = EvaluationKind::kRemoved;
    test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, forged)));
  }
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  if (print) std::cout << output.str();
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2 || argc == 3) {
    const std::string_view option{argv[1]};
    test::Check(argc == 2 || std::string_view{argv[2]} == "--twice");
    const auto count = argc == 3 ? 2U : 1U;
    if (option == "--source" || option == "--direct-source")
      Run(count, option == "--direct-source", true);
    else if (option == "--and-source" || option == "--and-direct-source")
      Run(count, option == "--and-direct-source", true, false, Selection::kAnd);
    else if (option == "--or-source" || option == "--or-direct-source")
      Run(count, option == "--or-direct-source", true, false, Selection::kOr);
    else test::Check(false);
    return 0;
  }
  for (const auto count : {1U, 2U, 200U, 201U, 400U, 800U})
    for (const auto selection : {Selection::kConditional, Selection::kAnd, Selection::kOr})
      for (const bool direct : {false, true}) Run(count, direct, false, false, selection);
  for (const auto selection : {Selection::kConditional, Selection::kAnd, Selection::kOr})
    for (const bool direct : {false, true}) Run(1, direct, false, true, selection);
}
