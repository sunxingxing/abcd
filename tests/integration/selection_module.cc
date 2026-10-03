#include "../../src/source/function_values.h"
#include "../../src/analysis/phased_value_import.h"
#include "../../src/source/exception_tree.h"
#include "../../src/source/module_guards.h"
#include "abcd/analysis/exception_modules.h"
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
enum class Selection { kConditional, kAnd, kOr, kDecisionAnd, kDecisionOr };
bool Decision(Selection selection) {
  return selection == Selection::kDecisionAnd || selection == Selection::kDecisionOr;
}
abc::MethodBody DecisionBody(unsigned count, bool direct, Selection selection, bool caught = false, bool one_arm = false) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr auto kGuard = abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16;
  const bool conjunction = selection == Selection::kDecisionAnd;
  std::vector<std::uint8_t> bytes;
  for (unsigned i = 0; i < count; ++i) {
    // Two predicates share one statement arm. The module read/guard belongs
    // only to the conditional RHS, even when either arm returns no value.
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 3,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0, op(abc::Opcode::kISTRUE)});
    const auto first = bytes.size();
    bytes.insert(bytes.end(), {op(conjunction ? abc::Opcode::kJEQZ_IMM8 : abc::Opcode::kJNEZ_IMM8), 0,
        op(abc::Opcode::kLDEXTERNALMODULEVAR_IMM8), 0, op(kGuard),
        static_cast<std::uint8_t>(static_cast<std::uint16_t>(kGuard) >> kByteBits), 3, 0});
    if (!direct) bytes.insert(bytes.end(), {op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 2, 0});
    if (!one_arm) bytes.push_back(op(abc::Opcode::kISTRUE));
    const auto second = bytes.size();
    if (!one_arm) bytes.insert(bytes.end(), {op(conjunction ? abc::Opcode::kJEQZ_IMM8 : abc::Opcode::kJNEZ_IMM8), 0});
    const auto arm = [&](std::uint8_t name) {
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 3,
          op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, name, 0,
          op(abc::Opcode::kCALLTHIS0_IMM8_V8), 0, 3});
    };
    arm(conjunction ? 0 : 3);
    if (one_arm) {
      bytes[first + 1] = static_cast<std::uint8_t>(bytes.size() - first);
      continue;
    }
    const auto jump = bytes.size(); bytes.insert(bytes.end(), {op(abc::Opcode::kJMP_IMM8), 0});
    bytes[first + 1] = static_cast<std::uint8_t>(bytes.size() - first);
    bytes[second + 1] = static_cast<std::uint8_t>(bytes.size() - second);
    arm(conjunction ? 3 : 0);
    bytes[jump + 1] = static_cast<std::uint8_t>(bytes.size() - jump);
  }
  bytes.push_back(op(abc::Opcode::kRETURNUNDEFINED));
  const auto handler_pc = static_cast<std::uint32_t>(bytes.size());
  if (caught) bytes.push_back(op(abc::Opcode::kRETURN));
  auto body = test::Body(bytes);
  if (caught) {
    abc::TryRegion region; region.offset = {0}; region.length = handler_pc; region.handler_count = 1;
    abc::CatchHandler handler; handler.offset = {handler_pc}; handler.length = 1;
    body.tries.push_back(region); body.handlers.push_back(handler);
  }
  return body;
}
abc::MethodBody Body(unsigned count, bool direct, bool barrier, Selection selection, bool caught = false) {
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
  const auto handler_pc = static_cast<std::uint32_t>(bytes.size());
  if (caught) bytes.push_back(op(abc::Opcode::kRETURN));
  auto body = test::Body(bytes);
  if (caught) {
    abc::TryRegion region; region.offset = {0}; region.length = handler_pc; region.handler_count = 1;
    abc::CatchHandler handler; handler.offset = {handler_pc}; handler.length = 1;
    body.tries.push_back(region); body.handlers.push_back(handler);
  }
  return body;
}
void Run(unsigned count, bool direct, bool print, bool barrier = false,
    Selection selection = Selection::kConditional, abc::ProfileId profile = abc::ProfileId::kDynamic1206) {
  const auto body = Decision(selection) ? DecisionBody(count, direct, selection) : Body(count, direct, barrier, selection);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(3, 1, {}));
  RouteInputIdentity identity; identity.profile = profile;
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
  test::Check(logical == (Decision(selection) ? 0 : count));
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  if (Decision(selection)) {
    unsigned decisions = 0;
    for (const auto& node : control.nodes) decisions += node.decision.has_value();
    test::Check(decisions == count);
  }
  FunctionSyntax syntax{};
  syntax.identity = identity; syntax.method = input.Method(); syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"value", u"method", u"probe", u"option", u"other", u"imported"}; syntax.texts = {u"on"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}};
  syntax.modules = {{{{7}, {0}}, NameId{5}, ModuleAccess::kReadOnly, NameEvidence::kMetadata}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (const auto* read = std::get_if<ValueNamedRead>(&graph.ops[i].operation)) constants[i] = NameId{read->name.value + 1};
    if (std::holds_alternative<ValueString>(graph.ops[i].operation)) constants[i] = TextId{0};
  }
  const ExpressionConstants resolved{constants, 6, 1};
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
    if (Decision(selection)) {
      const auto guard = *modules.front().pair.guard;
      std::optional<ExprId> other;
      for (const auto& placement : tree.operations)
        if (const auto* expression = std::get_if<ExprId>(&placement.node);
            expression && *expression != *flow.expressions[guard.value]) { other = *expression; break; }
      test::Check(other.has_value());
      auto wrong = tree;
      for (auto& placement : wrong.operations) if (placement.operation == guard) placement.node = *other;
      test::Check(std::holds_alternative<TreeFlowError>(VerifyTreeFlow(graph, control, evaluation, wrong)));
    }
  }
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  if (print) std::cout << output.str();
}

void RunPhased(unsigned count, bool direct, bool print, Selection selection,
    abc::ProfileId profile = abc::ProfileId::kDynamic1206, bool one_arm = false) {
  const auto body = Decision(selection) ? DecisionBody(count, direct, selection, true, one_arm) :
      Body(count, direct, false, selection, true);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(3, 1, {}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers));
  IdfQuota quota{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
  std::vector<EntryValueFact> entries;
  for (std::size_t i = 0; i < ssa.Values().size(); ++i) {
    const auto& value = ssa.Values()[i];
    const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
    if (reg && reg->value >= frame.locals && std::holds_alternative<MethodEntry>(value.origin))
      entries.push_back({{static_cast<std::uint32_t>(i)}, ValueKinds::Language()});
  }
  const auto modules = std::get<ExceptionModules>(ProveExceptionModules(flow, ssa));
  const auto env = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
      value_work, {}, PendingExceptionValues::kLanguage, modules.loads));
  const auto proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, env, values));
  const PhasedValueInput input{flow, ssa, env, values, proof, frame};
  const auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  const auto guards = std::get<ModuleGuards>(BuildPhasedModuleGuards(input, graph));
  std::vector<ModuleEvaluation> bindings;
  for (const auto& pair : guards.pairs)
    bindings.push_back({pair, std::get<ValueModuleRead>(graph.ops[pair.read.value].operation), {{7}, {0}}});
  const auto evaluation = std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(
      graph, {}, IncomingCells::kReject, {}, bindings));
  if (!Decision(selection)) {
    // Until phased selections own the whole argument expression, preserve the
    // native receiver gate. Never lower the retained callee through .call.
    const auto calls = BuildPhasedCallPlan(graph, evaluation);
    test::Check(std::get<CallPlanError>(calls) == CallPlanError::kReference);
    return;
  }
  const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
  const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
  unsigned decisions = 0;
  for (const auto& node : control.skeleton.nodes) decisions += node.decision.has_value();
  test::Check(decisions == (one_arm || !Decision(selection) ? 0 : count));
  const ExceptionTreeInput context{body, input, regions, control};
  FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"value", u"method", u"probe", u"option", u"other", u"imported"};
  syntax.texts = {u"on"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}};
  syntax.modules = {{{{7}, {0}}, NameId{5}, ModuleAccess::kReadOnly, NameEvidence::kMetadata}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (const auto* read = std::get_if<ValueNamedRead>(&graph.ops[i].operation)) constants[i] = NameId{read->name.value + 1};
    if (std::holds_alternative<ValueString>(graph.ops[i].operation)) constants[i] = TextId{0};
  }
  const ExpressionConstants resolved{constants, syntax.names.size(), syntax.texts.size()};
  const auto built = BuildExceptionFunctionDraft(graph, context, evaluation, syntax, resolved);
  test::Check(std::holds_alternative<ExceptionFunctionDraft>(built));
  const auto& tree = std::get<ExceptionFunctionDraft>(built).tree;
  if (one_arm) {
    unsigned branches = 0;
    for (const auto& node : tree.statements) if (const auto* branch = std::get_if<IfStmt>(&node.statement)) {
      ++branches;
      test::Check(!branch->alternate);
      const auto& body = std::get<BlockStmt>(tree.statements[branch->consequent.value].statement);
      test::Check(body.child_count != 0);
    }
    test::Check(branches == count);
  }
  test::Check(std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(graph, context, evaluation, tree, resolved)));
  const auto tree_flow = std::get<TreeFlow>(VerifyExceptionTreeFlow(graph, context, evaluation, tree));
  for (const auto& binding : bindings) {
    const auto read = binding.pair.read.value, guard = binding.pair.guard->value;
    test::Check(tree_flow.expressions[read] && tree_flow.expressions[read] == tree_flow.expressions[guard]);
  }
  if (count == 1) {
    auto forged = evaluation; forged.modules.front().pair.guard.reset();
    test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
    auto wrong = tree;
    const auto guard = *bindings.front().pair.guard;
    for (auto& placement : wrong.operations) if (placement.operation == guard) placement.node = AnchorId{0};
    test::Check(std::holds_alternative<TreeFlowError>(VerifyExceptionTreeFlow(graph, context, evaluation, wrong)));
  }
  if (print) {
    std::ostringstream output;
    test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
    std::cout << output.str();
  }
}

}  // namespace
int main(int argc, char** argv) {
  if (argc == 2 || argc == 3) {
    const std::string_view option{argv[1]};
    test::Check(argc == 2 || std::string_view{argv[2]} == "--twice");
    const auto count = argc == 3 ? 2U : 1U;
    if (option == "--phased-selection-refusal")
      RunPhased(count, false, false, Selection::kConditional);
    else if (option == "--phased-one-arm-source" || option == "--phased-one-arm-direct-source")
      RunPhased(count, option == "--phased-one-arm-direct-source", true, Selection::kDecisionAnd,
          abc::ProfileId::kDynamic1206, true);
    else if (option == "--phased-decision-and-source" || option == "--phased-decision-and-direct-source")
      RunPhased(count, option == "--phased-decision-and-direct-source", true, Selection::kDecisionAnd);
    else if (option == "--phased-decision-or-source" || option == "--phased-decision-or-direct-source")
      RunPhased(count, option == "--phased-decision-or-direct-source", true, Selection::kDecisionOr);
    else if (option == "--source" || option == "--direct-source")
      Run(count, option == "--direct-source", true);
    else if (option == "--and-source" || option == "--and-direct-source")
      Run(count, option == "--and-direct-source", true, false, Selection::kAnd);
    else if (option == "--or-source" || option == "--or-direct-source")
      Run(count, option == "--or-direct-source", true, false, Selection::kOr);
    else if (option == "--decision-and-source" || option == "--decision-and-direct-source")
      Run(count, option == "--decision-and-direct-source", true, false, Selection::kDecisionAnd);
    else if (option == "--decision-or-source" || option == "--decision-or-direct-source")
      Run(count, option == "--decision-or-direct-source", true, false, Selection::kDecisionOr);
    else test::Check(false);
    return 0;
  }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto count : {1U, 2U, 200U, 201U})
      for (const auto selection : {Selection::kDecisionAnd, Selection::kDecisionOr})
        for (const bool direct : {false, true}) {
          RunPhased(count, direct, false, selection, profile);
          if (selection == Selection::kDecisionAnd) RunPhased(count, direct, false, selection, profile, true);
        }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto count : {1U, 2U, 200U, 201U, 402U})
      for (const auto selection : {Selection::kDecisionAnd, Selection::kDecisionOr})
        for (const bool direct : {false, true}) Run(count, direct, false, false, selection, profile);
  for (const auto count : {1U, 2U, 200U, 201U, 400U, 800U})
    for (const auto selection : {Selection::kConditional, Selection::kAnd, Selection::kOr})
      for (const bool direct : {false, true}) Run(count, direct, false, false, selection);
  for (const auto selection : {Selection::kConditional, Selection::kAnd, Selection::kOr})
    for (const bool direct : {false, true}) Run(1, direct, false, true, selection);
}
