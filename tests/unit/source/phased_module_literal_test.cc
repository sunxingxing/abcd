#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/evaluation_plan.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/family_print.h"
#include "../../../src/source/module_guards.h"
#include "../../../src/source/module_evaluation.h"
#include "../../../src/source/tree_flow.h"
#include "abcd/analysis/exception_modules.h"
#include "analysis/ssa_fixture.h"

#include <sstream>
#include <string_view>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
std::uint64_t Run(abc::ProfileId profile, std::uint32_t fields, bool caught,
                  bool call, bool split_dispatch = false, std::ostream* output = nullptr,
                  bool array = false, bool snapshot = false) {
  constexpr std::uint8_t kObject = 0, kReceiver = 1, kSaved = 2, kParameter = 3;
  constexpr unsigned kByteBits = 8;
  std::vector<std::uint8_t> bytes;
  const auto opcode = [&](abc::Opcode op) {
    bytes.push_back(static_cast<std::uint8_t>(op));
    if (static_cast<std::uint16_t>(op) > UINT8_MAX)
      bytes.push_back(static_cast<std::uint16_t>(op) >> kByteBits);
  };
  const auto load = [&](std::uint8_t reg) { opcode(abc::Opcode::kLDA_V8); bytes.push_back(reg); };
  const auto store = [&](std::uint8_t reg) { opcode(abc::Opcode::kSTA_V8); bytes.push_back(reg); };
  load(kParameter); store(kSaved);
  if (snapshot) {
    opcode(abc::Opcode::kLDEXTERNALMODULEVAR_IMM8); bytes.push_back(0);
    opcode(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16); bytes.insert(bytes.end(), {0, 0});
    opcode(abc::Opcode::kLDTRUE); store(kSaved);
  }
  const auto allocation_pc = static_cast<std::uint32_t>(bytes.size());
  opcode(array ? abc::Opcode::kCREATEEMPTYARRAY_IMM8 : abc::Opcode::kCREATEEMPTYOBJECT);
  if (array) bytes.push_back(0);
  store(kObject);
  const auto initialization_pc = static_cast<std::uint32_t>(bytes.size());
  for (std::uint32_t i = 0; i < fields; ++i) {
    opcode(abc::Opcode::kLDEXTERNALMODULEVAR_IMM8); bytes.push_back(i % 2);
    opcode(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16);
    bytes.insert(bytes.end(), {static_cast<std::uint8_t>(i % 2), 0});
    if (call) store(kReceiver);
    opcode(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.insert(bytes.end(), {0, 2, 0});
    if (call) { opcode(abc::Opcode::kCALLTHIS0_IMM8_V8); bytes.insert(bytes.end(), {0, kReceiver}); }
    if (array) {
      opcode(abc::Opcode::kSTOWNBYINDEX_IMM8_V8_IMM16);
      bytes.insert(bytes.end(), {0, kObject, static_cast<std::uint8_t>(i),
          static_cast<std::uint8_t>(i >> kByteBits)});
    } else {
      opcode(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8);
      const auto name = i + 3;
      bytes.insert(bytes.end(), {0, static_cast<std::uint8_t>(name),
          static_cast<std::uint8_t>(name >> kByteBits), kObject});
    }
  }
  load(kObject); opcode(abc::Opcode::kRETURN);
  const auto handler_pc = static_cast<std::uint32_t>(bytes.size());
  if (caught) { load(kSaved); opcode(abc::Opcode::kRETURN); }
  auto body = test::Body(bytes);
  if (caught) {
    abc::TryRegion region;
    region.offset = {snapshot ? 0 : split_dispatch ? initialization_pc : allocation_pc};
    region.length = handler_pc - region.offset.value; region.handler_count = 1;
    body.tries.push_back(region);
    abc::CatchHandler handler; handler.offset = {handler_pc};
    handler.length = static_cast<std::uint32_t>(bytes.size()) - handler_pc;
    body.handlers.push_back(handler);
  }
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(3, 1, {}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers));
  IdfQuota quota{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
  std::vector<EntryValueFact> entries;
  for (std::size_t i = 0; i < ssa.Values().size(); ++i) {
    const auto& value = ssa.Values()[i];
    if (const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
        reg && reg->value >= frame.locals && std::holds_alternative<MethodEntry>(value.origin))
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
  for (const auto& pair : guards.pairs) {
    const auto read = std::get<ValueModuleRead>(graph.ops[pair.read.value].operation);
    bindings.push_back({pair, read, {{1}, {read.slot.value}}});
  }
  const auto built = BuildPhasedEvaluationPlan(graph, {}, IncomingCells::kReject, {}, bindings);
  if ((split_dispatch && !array) || snapshot) {
    test::Check(std::get<EvaluationError>(built) == EvaluationError::kUnsupportedOperation);
    return 0;
  }
  test::Check(std::holds_alternative<EvaluationPlan>(built));
  const auto& evaluation = std::get<EvaluationPlan>(built);
  test::Check(evaluation.literals && evaluation.literals->definitions.size() == fields);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedEvaluationPlan(graph, evaluation)));
  for (const auto& pair : guards.pairs) {
    test::Check(evaluation.operations[pair.read.value].kind == EvaluationKind::kNestedLiteral);
    test::Check(evaluation.operations[pair.guard->value].kind == EvaluationKind::kModuleGuard);
  }
  if (fields != 2 && !array) return evaluation.literals->work;
  const auto index = std::get<ModuleEvaluationIndex>(IndexModuleEvaluations(graph, evaluation.modules));
  const auto checked = std::get<std::uint64_t>(VerifyPhasedLiteralPlan(
      graph, *evaluation.literals, evaluation, {}, index.peers));
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedLiteralPlan(
      graph, *evaluation.literals, evaluation, {SemanticBudget{}.bytes, checked}, index.peers)));
  test::Check(std::get<LiteralError>(VerifyPhasedLiteralPlan(graph, *evaluation.literals,
      evaluation, {SemanticBudget{}.bytes, checked - 1}, index.peers)) == LiteralError::kWorkLimit);
  // A standalone literal call has no authority to invent module pair ownership.
  test::Check(std::holds_alternative<LiteralError>(VerifyPhasedLiteralPlan(
      graph, *evaluation.literals, evaluation)));
  test::Check(std::get<LiteralError>(BuildPhasedLiteralPlan(graph, evaluation)) == LiteralError::kLateValue);
  auto changed = evaluation;
  changed.modules.front().pair.guard.reset();
  test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, changed)));
  changed = evaluation; ++changed.modules.front().read.slot.value;
  test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, changed)));
  changed = evaluation; changed.operations[guards.pairs.front().guard->value].kind = EvaluationKind::kRemoved;
  test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, changed)));
  changed = evaluation; changed.literals->nested[guards.pairs.front().read.value].reset();
  test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, changed)));
  changed = evaluation;
  std::swap(changed.literals->definitions.front(), changed.literals->definitions.back());
  test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, changed)));
  if (caught) {
    auto rerouted = graph;
    for (auto& exit : rerouted.exceptional_exits)
      if (exit.op == guards.pairs.front().guard) exit.handler.reset();
    test::Check(std::holds_alternative<EvaluationError>(BuildPhasedEvaluationPlan(
        rerouted, {}, IncomingCells::kReject, {}, bindings)));
    // Independently check the guard itself, without relying on the evaluation
    // verifier's earlier read/guard-dispatch comparison to reject this mutation.
    test::Check(std::get<LiteralError>(VerifyPhasedLiteralPlan(rerouted, *evaluation.literals,
        evaluation, {}, index.peers)) == LiteralError::kObserved);
  }
  const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
  const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
  FunctionSyntax syntax{};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.parameters.resize(1); syntax.names = {u"moduleFirst", u"moduleSecond", u"read", u"field0", u"field1"};
  if (array) {
    syntax.names.resize(3);
    for (std::uint32_t i = 0; i < fields; ++i) {
      const auto key = std::to_string(i); syntax.names.emplace_back(key.begin(), key.end());
    }
  }
  syntax.modules = {{{{1}, {0}}, {0}, ModuleAccess::kReadOnly, NameEvidence::kSynthesized},
      {{{1}, {1}}, {1}, ModuleAccess::kReadOnly, NameEvidence::kSynthesized}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (std::holds_alternative<ValueNamedRead>(graph.ops[i].operation)) constants[i] = NameId{2};
  for (std::size_t i = 0; i < evaluation.literals->definitions.size(); ++i)
    constants[evaluation.literals->definitions[i].value] = NameId{static_cast<std::uint32_t>(i + 3)};
  const ExceptionTreeInput context{body, input, regions, control};
  const auto constructed = BuildExceptionFunctionDraft(
      graph, context, evaluation, syntax, {constants, syntax.names.size()});
  if (split_dispatch) {
    test::Check(std::get<FunctionTreeError>(constructed) == FunctionTreeError::kSemantics);
    return evaluation.literals->work;
  }
  const auto& draft = std::get<ExceptionFunctionDraft>(constructed);
  const auto correspondence = std::get<TreeFlow>(VerifyExceptionTreeFlow(graph, context, evaluation, draft.tree));
  for (const auto& pair : guards.pairs) {
    const auto expression = correspondence.expressions[pair.read.value];
    test::Check(expression && expression == correspondence.expressions[pair.guard->value]);
    const auto origin = std::get<CombinedOrigin>(
        draft.tree.origins[draft.tree.expressions[expression->value].origin.value]);
    test::Check(origin.part_count == 2);
    test::Check(std::get<OriginalSite>(draft.tree.origins[draft.tree.origin_parts[origin.first_part].value]).pc ==
        graph.origins[pair.read.value].original.value_or(graph.origins[pair.read.value].pc));
    test::Check(std::get<OriginalSite>(draft.tree.origins[draft.tree.origin_parts[origin.first_part + 1].value]).pc ==
        graph.origins[pair.guard->value].original.value_or(graph.origins[pair.guard->value].pc));
  }
  const auto family = std::get<ConstructedFamily>(BuildFunctionFamily({{draft.tree, {}, {}}}, draft.tree.owner));
  std::ostringstream source;
  test::Check(std::holds_alternative<PrintedFamilyDraft>(PrintFamilyDraft(family, source)));
  if (output) *output << source.str() << ",\n";
  return array ? draft.work : evaluation.literals->work;
}
}  // namespace
int main(int argc, char** argv) {
  const bool emit = argc == 2 && std::string_view(argv[1]) == "--emit";
  if (emit) std::cout << "export const literals = [\n";
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                            abc::ProfileId::kDynamic1301})
    for (const bool call : {false, true})
      for (const bool caught : {false, true}) {
        std::uint64_t previous = 0;
        for (const auto fields : {2U, 200U, 201U, 400U}) {
          const auto work = Run(profile, fields, caught, call, false,
              emit && fields == 2 ? &std::cout : nullptr);
          if (fields == 400) test::Check(work < previous * 3);
          previous = work;
        }
        if (caught) Run(profile, 2, true, call, true);
      }
  if (emit) std::cout << "];\n";
  if (emit) std::cout << "export const arrays = [\n";
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                            abc::ProfileId::kDynamic1301})
    for (const bool call : {false, true})
      for (const bool caught : {false, true}) {
        std::uint64_t previous = 0;
        for (const auto fields : {2U, 200U, 201U, 400U}) {
          const auto work = Run(profile, fields, caught, call, false,
              emit && fields == 2 ? &std::cout : nullptr, true);
          if (fields == 400) test::Check(work < previous * 3);
          previous = work;
        }
        if (caught) {
          Run(profile, 2, true, call, true, nullptr, true);
          Run(profile, 2, true, call, false, nullptr, true, true);
        }
      }
  if (emit) std::cout << "];\n";
}
