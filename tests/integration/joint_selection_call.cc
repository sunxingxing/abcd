#include "abcd/source/source_route.h"
#include "../../src/analysis/phased_value_import.h"
#include "../../src/source/exception_tree.h"
#include "../../src/source/function_tree.h"
#include "abcd/analysis/exception_modules.h"
#include "../../src/source/call_plan.h"
#include "../../src/source/phase_evaluation.h"
#include "../../src/source/tree_semantics.h"
#include "analysis/ssa_fixture.h"
#include "abcd/source/tree_printer.h"

#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class Form { kPlain, kPrefix, kWrapped, kReversed, kBarrier };
abc::MethodBody Body(unsigned arguments, unsigned copies, Form form) {
  test::Check(arguments > 0 && arguments + 2 < UINT8_MAX);
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  const auto callee = static_cast<std::uint8_t>(arguments + 1);
  const auto parameter = static_cast<std::uint8_t>(arguments + (form == Form::kWrapped ? 3 : 2));
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kMOV_V8_V8), 0, parameter};
  if (form == Form::kWrapped) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 0,
      op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 3, 0, op(abc::Opcode::kSTA_V8), static_cast<std::uint8_t>(arguments + 2)});
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0,
      op(abc::Opcode::kSTA_V8), callee});
  for (unsigned argument = 0; argument < arguments; ++argument) {
    for (unsigned copy = 0; copy < copies; ++copy)
      bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), 0, 0});
    if (form == Form::kPrefix) bytes.insert(bytes.end(),
        {op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 3, 0});
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 0,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0, op(abc::Opcode::kISTRUE)});
    const auto branch = bytes.size(); bytes.insert(bytes.end(), {op(abc::Opcode::kJEQZ_IMM8), 0});
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDAI_IMM32), 7, 0, 0, 0});
    const auto jump = bytes.size(); bytes.insert(bytes.end(), {op(abc::Opcode::kJMP_IMM8), 0});
    bytes[branch + 1] = static_cast<std::uint8_t>(bytes.size() - branch);
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 0,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 2, 0});
    bytes[jump + 1] = static_cast<std::uint8_t>(bytes.size() - jump);
    const auto slot = form == Form::kReversed ? arguments - argument : argument + 1;
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), static_cast<std::uint8_t>(slot)});
    if (form == Form::kBarrier) bytes.push_back(op(abc::Opcode::kDEBUGGER));
  }
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), callee,
      op(abc::Opcode::kCALLTHISRANGE_IMM8_IMM8_V8), 0, static_cast<std::uint8_t>(arguments), 0});
  if (form == Form::kWrapped) bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 1,
      op(abc::Opcode::kLDA_V8), static_cast<std::uint8_t>(arguments + 2),
      op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, 0, 1});
  bytes.push_back(op(abc::Opcode::kRETURN));
  const auto end = static_cast<std::uint32_t>(bytes.size());
  bytes.push_back(op(abc::Opcode::kRETURN));
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {0}; region.length = end; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {end}; handler.length = 1;
  body.tries.push_back(region); body.handlers.push_back(handler);
  return body;
}
std::uint64_t Run(unsigned arguments, unsigned copies, abc::ProfileId profile, Form form, bool print) {
  const auto body = Body(arguments, copies, form);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(arguments + (form == Form::kWrapped ? 3 : 2), 1, {}));
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
      value_work, {}, PendingExceptionValues::kLanguage, modules.loads, {}, NormalCallResults(kSourceContract)));
  const auto proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, env, values));
  const PhasedValueInput input{flow, ssa, env, values, proof, frame};
  const auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  const auto planned = BuildPhasedEvaluationPlan(graph);
  test::Check(std::holds_alternative<EvaluationPlan>(planned));
  const auto& evaluation = std::get<EvaluationPlan>(planned);
  if (form == Form::kBarrier || (form == Form::kReversed && arguments > 1)) {
    test::Check(evaluation.phase_calls.empty());
    test::Check(std::get<CallPlanError>(BuildPhasedCallPlan(graph, evaluation)) == CallPlanError::kReference);
    return evaluation.work;
  }
  test::Check(evaluation.phase_calls.size() == arguments);
  test::Check(std::holds_alternative<CallPlan>(BuildPhasedCallPlan(graph, evaluation)));
  if (form == Form::kWrapped) test::Check(evaluation.phase_calls.front().ancestors.size() == 1);
  for (unsigned i = 1; i < arguments; ++i) {
    test::Check(evaluation.phase_calls[i].callee == evaluation.phase_calls[0].callee);
    test::Check(evaluation.phase_calls[i - 1].argument < evaluation.phase_calls[i].argument);
  }
  if (arguments <= 3) {
    const auto work = std::get<std::uint64_t>(VerifyPhasedEvaluationPlan(graph, evaluation));
    SemanticBudget exact; exact.work = work;
    test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedEvaluationPlan(graph, evaluation, exact)));
    --exact.work;
    test::Check(std::get<EvaluationError>(VerifyPhasedEvaluationPlan(graph, evaluation, exact)) == EvaluationError::kWorkLimit);
    SemanticBudget no_bytes; no_bytes.bytes = 0;
    test::Check(std::get<EvaluationError>(VerifyPhasedEvaluationPlan(graph, evaluation, no_bytes)) == EvaluationError::kResourceLimit);
    auto forged = evaluation; forged.phase_calls.pop_back();
    test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
    if (arguments > 1) {
      forged = evaluation; std::swap(forged.phase_calls[0].argument, forged.phase_calls[1].argument);
      test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
      forged = evaluation; forged.phase_calls[1].callee = forged.phase_calls[1].branch;
      test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
    }
  }
  const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
  const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
  const ExceptionTreeInput context{body, input, regions, control};
  FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"value", u"method", u"probe", u"option", u"other"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (const auto* read = std::get_if<ValueNamedRead>(&graph.ops[i].operation)) constants[i] = NameId{read->name.value + 1};
  const ExpressionConstants resolved{constants, syntax.names.size(), 0};
  const auto built = BuildExceptionFunctionDraft(graph, context, evaluation, syntax, resolved);
  test::Check(std::holds_alternative<ExceptionFunctionDraft>(built));
  const auto& tree = std::get<ExceptionFunctionDraft>(built).tree;
  test::Check(tree.recovery_issues.empty());
  test::Check(std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(graph, context, evaluation, tree, resolved)));
  test::Check(std::holds_alternative<TreeFlow>(VerifyExceptionTreeFlow(graph, context, evaluation, tree)));
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  if (print) std::cout << output.str();
  return evaluation.work;
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 3) {
    const unsigned arguments = std::stoul(argv[2]);
    Run(arguments, 1, abc::ProfileId::kDynamic1206,
        std::string_view{argv[1]} == "--prefix-source" ? Form::kPrefix :
        std::string_view{argv[1]} == "--wrapped-source" ? Form::kWrapped : Form::kPlain, true);
    return 0;
  }
  unsigned scenarios = 0;
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    std::uint64_t previous_work = 0;
    for (const auto arguments : {1U, 2U, 3U, 50U, 100U, 200U, 201U}) {
      for (const auto form : {Form::kPlain, Form::kPrefix, Form::kWrapped, Form::kReversed, Form::kBarrier}) {
        const auto work = Run(arguments, 1, profile, form, false); ++scenarios;
        if (form == Form::kWrapped && arguments >= 50) {
          if (previous_work) test::Check(work <= previous_work * 3);
          previous_work = work;
          if (profile == abc::ProfileId::kDynamic1206) std::cout << arguments << " arguments: " << work << " evaluation steps\n";
        }
      }
    }
    for (const auto copies : {2U, 200U, 201U, 402U})
      for (const auto form : {Form::kPrefix, Form::kWrapped}) { Run(3, copies, profile, form, false); ++scenarios; }
  }
  std::cout << scenarios << " decoded joint-call scenarios passed\n";
}
