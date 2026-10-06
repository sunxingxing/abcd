#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/phase_evaluation.h"
#include "../../../src/source/evaluation_liveness.h"
#include "../../support/analysis/ssa_fixture.h"
#include "abcd/analysis/exception_locals.h"
#include "abcd/source/tree_printer.h"
#include <sstream>
#include <nlohmann/json.hpp>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
namespace {
std::string Source(abc::ProfileId profile, unsigned locals, unsigned copies, bool split, bool caught, bool initializer) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  std::vector<std::uint8_t> bytes;
  const auto emit = [&](abc::Opcode code, unsigned operand) { bytes.push_back(op(code)); bytes.push_back(static_cast<std::uint8_t>(operand)); };
  const auto boundary = [&] { if (split) emit(abc::Opcode::kJMP_IMM8, 2); };
  emit(abc::Opcode::kLDA_V8, locals); emit(abc::Opcode::kSTA_V8, 0);
  const auto begin = bytes.size(); emit(abc::Opcode::kLDA_V8, 0);
  for (unsigned i = 0; i < copies; ++i) { emit(abc::Opcode::kSTA_V8, locals - 1); boundary(); emit(abc::Opcode::kLDA_V8, locals - 1); }
  bytes.push_back(op(abc::Opcode::kISTRUE));
  const auto branch = bytes.size(); emit(abc::Opcode::kJEQZ_IMM8, 0);
  emit(abc::Opcode::kLDA_V8, 0); emit(abc::Opcode::kTYPEOF_IMM8, 0); emit(abc::Opcode::kSTA_V8, 1);
  emit(abc::Opcode::kLDA_V8, locals + 2); bytes.insert(bytes.end(), {op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 1});
  emit(abc::Opcode::kLDA_V8, locals + 1); bytes.insert(bytes.end(), {op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 0});
  emit(abc::Opcode::kSTA_V8, 0); boundary();
  const auto back = bytes.size(); test::Check(back - begin <= static_cast<unsigned>(std::numeric_limits<std::int8_t>::max()));
  emit(abc::Opcode::kJMP_IMM8, static_cast<std::uint8_t>(-static_cast<int>(back - begin)));
  bytes[branch + 1] = static_cast<std::uint8_t>(bytes.size() - branch);
  const auto end = bytes.size(); emit(abc::Opcode::kLDA_V8, 0); bytes.push_back(op(abc::Opcode::kRETURN));
  const auto handler_pc = bytes.size();
  if (caught) { emit(abc::Opcode::kSTA_V8, 1); emit(abc::Opcode::kLDA_V8, 1); }
  else emit(abc::Opcode::kLDA_V8, 0);
  bytes.push_back(op(abc::Opcode::kRETURN));
  auto body = test::Body(bytes);
  const auto protected_begin = initializer ? 0 : begin;
  abc::TryRegion region; region.offset = {static_cast<std::uint32_t>(protected_begin)};
  region.length = static_cast<std::uint32_t>(end - protected_begin); region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {static_cast<std::uint32_t>(handler_pc)}; handler.length = static_cast<std::uint32_t>(bytes.size() - handler_pc);
  body.tries = {region}; body.handlers = {handler};
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(locals, 3, {}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers, {}, {}, &frame));
  IdfQuota quota{kDefaultSemanticWork}; const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
  std::vector<EntryValueFact> entries;
  for (std::size_t v = 0; v < ssa.Values().size(); ++v) {
    const auto& value = ssa.Values()[v]; const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
    if (reg && reg->value >= frame.locals && std::holds_alternative<MethodEntry>(value.origin))
      entries.push_back({{static_cast<std::uint32_t>(v)}, ValueKinds::Language()});
  }
  const auto env = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  const auto lexical = std::get<ExceptionLocals>(ProveExceptionLocals(flow, env));
  IdfQuota value_quota{kDefaultSemanticWork};
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
      value_quota, {}, PendingExceptionValues::kLanguage, lexical.loads, {}, CallResultValues::kLanguage));
  const auto proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, env, values));
  const PhasedValueInput input{flow, ssa, env, values, proof, frame};
  const auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  const auto evaluation = std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));
  for (std::size_t v = 0; v < graph.values.size(); ++v) {
    const auto& value = graph.values[v]; if (value.life != ValueLife::kLive) continue;
    if (std::holds_alternative<PhiDefinition>(value.origin))
      test::Check(RetainedEvaluationLifetime(graph, evaluation, {static_cast<std::uint32_t>(v)}) == EvaluationStorageLifetime::kInvocation);
    if (std::holds_alternative<CatchDefinition>(value.origin))
      test::Check(RetainedEvaluationLifetime(graph, evaluation, {static_cast<std::uint32_t>(v)}) == EvaluationStorageLifetime::kHandler);
  }
  std::size_t copies_of_phi = 0;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    const auto& operation = graph.ops[i]; if (!std::holds_alternative<ValueCopy>(operation.operation)) continue;
    const auto operand = ResolveEvaluationOperand(evaluation, graph.uses[operation.first_use.value].operand);
    const auto* value = std::get_if<ValueId>(&operand);
    if (!value || !std::holds_alternative<PhiDefinition>(graph.values[value->value].origin)) continue;
    const auto& placement = evaluation.operations[i];
    if (placement.kind == EvaluationKind::kInline) ++copies_of_phi;
  }
  // This loop needs storage for x, but no second storage merely to read x in
  // its condition/type observation before any incoming phi copy changes it.
  test::Check(copies_of_phi != 0);
  const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
  const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
  const ExceptionTreeInput exceptions{body, input, regions, control};
  FunctionSyntax syntax{}; syntax.identity.profile = profile; syntax.method = {300};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.texts = {u"number"}; syntax.parameters.resize(3);
  const std::vector<ExpressionConstant> constants(graph.ops.size());
  const ExpressionConstants resolved{constants, 0, 1, TextId{0}};
  const auto built = BuildExceptionFunctionDraft(graph, exceptions, evaluation, syntax, resolved);
  test::Check(std::holds_alternative<ExceptionFunctionDraft>(built));
  const auto& tree = std::get<ExceptionFunctionDraft>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(graph, exceptions, evaluation, tree, resolved)));
  std::ostringstream source; test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, source))); return source.str();
}
}
int main(int argc, char**) {
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (unsigned locals : {2U, 7U}) for (unsigned copies : {0U, 1U, 4U}) for (bool split : {false, true})
      for (bool caught : {false, true}) for (bool initializer : {false, true}) {
      const auto source = Source(profile, locals, copies, split, caught, initializer);
      if (argc > 1) std::cout << nlohmann::json{{"source", source}, {"caught", caught}, {"initializer", initializer}} << '\n';
    }
}
