#include "../../../src/source/module_guards.h"
#include "../../../src/analysis/phased_value_import.h"
#include "abcd/analysis/exception_modules.h"
#include "analysis/ssa_fixture.h"
#include "source/action_regions.h"
#include "../../../src/source/evaluation_plan.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/family_print.h"
#include "../../../src/source/tree_flow.h"
#include "../../../src/analysis/value_edits.h"
#include <sstream>
#include <string_view>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
void Run(std::uint32_t count, bool caught, bool wide, bool external, abc::ProfileId profile,
         std::ostream* output = nullptr, bool property = false, bool split_handler = false) {
  constexpr unsigned kByteBits = 8;
  constexpr std::uint8_t kModuleSlot = 7, kNameSlot = 3;
  std::vector<std::uint8_t> bytes;
  const auto opcode = [&](abc::Opcode op) {
    bytes.push_back(static_cast<std::uint8_t>(op));
    if (static_cast<std::uint16_t>(op) > UINT8_MAX)
      bytes.push_back(static_cast<std::uint16_t>(op) >> kByteBits);
  };
  for (std::uint32_t i = 0; i < count; ++i) {
    opcode(external ? (wide ? abc::Opcode::kWIDE_LDEXTERNALMODULEVAR_PREF_IMM16 :
        abc::Opcode::kLDEXTERNALMODULEVAR_IMM8) :
        (wide ? abc::Opcode::kWIDE_LDLOCALMODULEVAR_PREF_IMM16 : abc::Opcode::kLDLOCALMODULEVAR_IMM8));
    bytes.push_back(kModuleSlot);
    if (wide) bytes.push_back(0);
    opcode(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16);
    bytes.insert(bytes.end(), {kNameSlot, 0});
  }
  const auto pair_end = static_cast<std::uint32_t>(bytes.size());
  if (property) {
    opcode(abc::Opcode::kLDOBJBYNAME_IMM8_ID16);
    bytes.insert(bytes.end(), {0, kNameSlot, 0});
  }
  const auto end = static_cast<std::uint32_t>(bytes.size());
  opcode(abc::Opcode::kRETURN);
  if (caught) {
    opcode(abc::Opcode::kSTA_V8); bytes.push_back(0);
    opcode(abc::Opcode::kLDA_V8); bytes.push_back(0);
    opcode(abc::Opcode::kRETURN);
  }
  auto body = test::Body(bytes);
  if (caught) {
    abc::TryRegion region;
    region.length = split_handler ? pair_end : end; region.handler_count = 1;
    body.tries.push_back(region);
    abc::CatchHandler handler;
    handler.offset = {end + 1}; handler.length = static_cast<std::uint32_t>(bytes.size()) - handler.offset.value;
    body.handlers.push_back(handler);
  }
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(caught ? 1 : 0, 0, {}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers));
  IdfQuota quota{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
  const auto modules = std::get<ExceptionModules>(ProveExceptionModules(flow, ssa));
  test::Check(modules.reads.size() == count);
  const auto environments = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, {},
      value_work, {}, PendingExceptionValues::kLanguage, modules.loads));
  const auto proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, environments, values));
  const PhasedValueInput input{flow, ssa, environments, values, proof, frame};
  const auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  const auto built = BuildPhasedModuleGuards(input, graph);
  test::Check(std::holds_alternative<ModuleGuards>(built));
  const auto& plan = std::get<ModuleGuards>(built);
  test::Check(plan.pairs.size() == count);
  std::vector<ModuleEvaluation> bindings;
  for (const auto& pair : plan.pairs)
    bindings.push_back({pair, std::get<ValueModuleRead>(graph.ops[pair.read.value].operation), {{1}, {0}}});
  const auto evaluated = BuildPhasedEvaluationPlan(graph, {}, IncomingCells::kReject, {}, bindings);
  test::Check(std::holds_alternative<EvaluationPlan>(evaluated));
  const auto& evaluation = std::get<EvaluationPlan>(evaluated);
  if (property) test::Check(evaluation.operations[plan.pairs.back().read.value].kind ==
      (split_handler ? EvaluationKind::kRoot : EvaluationKind::kInline));
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedEvaluationPlan(graph, evaluation)));
  if (split_handler) {
    auto forged = evaluation;
    const auto read = plan.pairs.back().read;
    const auto consumer = plan.pairs.back().guard->value + 1;
    forged.operations[read.value] = {EvaluationKind::kInline, graph.ops[consumer].first_use, {}};
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, forged)));
    return;
  }
  if (count == 1) {
    if (caught) {
      const auto stored = graph.ops.size() - 3, loaded = stored + 1;
      test::Check(evaluation.operations[stored].kind == EvaluationKind::kRoot);
      test::Check(evaluation.operations[loaded].kind == EvaluationKind::kAlias);
      test::Check(evaluation.aliases[graph.ops[loaded].result->value] == *graph.ops[stored].result);
      auto escaped = evaluation;
      escaped.operations[stored] = {EvaluationKind::kAlias};
      escaped.aliases[graph.ops[stored].result->value] =
          std::get<ValueId>(graph.uses[graph.ops[stored].first_use.value].operand);
      escaped.aliases[graph.ops[loaded].result->value] = escaped.aliases[graph.ops[stored].result->value];
      test::Check(std::get<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, escaped)) == EvaluationError::kOwnership);
    }
    const auto verified = std::get<std::uint64_t>(VerifyPhasedEvaluationPlan(graph, evaluation));
    test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedEvaluationPlan(
        graph, evaluation, {SemanticBudget{}.bytes, verified})));
    test::Check(std::get<EvaluationFailure>(VerifyPhasedEvaluationPlan(
        graph, evaluation, {SemanticBudget{}.bytes, verified - 1})) == EvaluationError::kWorkLimit);
    auto omitted = evaluation;
    omitted.operations[plan.pairs.front().guard->value].kind = EvaluationKind::kRemoved;
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, omitted)));
    if (caught) {
      auto rerouted = graph;
      for (auto& exit : rerouted.exceptional_exits)
        if (exit.op == plan.pairs.front().guard) exit.handler.reset();
      test::Check(std::holds_alternative<EvaluationFailure>(BuildPhasedEvaluationPlan(
          rerouted, {}, IncomingCells::kReject, {}, bindings)));
    }
    const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
    const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
    FunctionSyntax syntax{};
    syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    syntax.names = {u"moduleValue"};
    syntax.modules.push_back({{{1}, {0}}, {0}, ModuleAccess::kReadOnly, NameEvidence::kSynthesized});
    std::vector<ExpressionConstant> constants(graph.ops.size());
    if (property) constants[plan.pairs.back().guard->value + 1] = NameId{0};
    const auto draft = BuildExceptionFunctionDraft(graph, {body, input, regions, control},
        evaluation, syntax, {constants, 1});
    test::Check(std::holds_alternative<ExceptionFunctionDraft>(draft));
    const auto& completed=std::get<ExceptionFunctionDraft>(draft);
    const auto& tree = completed.tree;
    test::Check(completed.flow&&completed.flow->original);
    test::CheckActionRegions(tree,*completed.flow->original);
    const auto family = std::get<ConstructedFamily>(BuildFunctionFamily({{tree, *completed.flow, {}}}, tree.owner));
    std::ostringstream source;
    const auto printed = PrintFamilyDraft(family, source);
    test::Check(std::holds_alternative<PrintedFamilyDraft>(printed));
    test::Check(tree.operations.size() == graph.ops.size());
    const auto correspondence = std::get<TreeFlow>(VerifyExceptionTreeFlow(
        graph, {body, input, regions, control}, evaluation, tree));
    const auto expression = correspondence.expressions[plan.pairs[0].read.value];
    test::Check(expression && expression == correspondence.expressions[plan.pairs[0].guard->value]);
    const auto origin = std::get<CombinedOrigin>(tree.origins[tree.expressions[expression->value].origin.value]);
    test::Check(origin.part_count == 2);
    test::Check(std::get<OriginalSite>(tree.origins[tree.origin_parts[origin.first_part].value]).pc ==
        body.instructions[0].offset);
    test::Check(std::get<OriginalSite>(tree.origins[tree.origin_parts[origin.first_part + 1].value]).pc ==
        body.instructions[1].offset);
    const auto range = std::get<PrintedFamilyDraft>(printed).members[0].expressions[expression->value];
    test::Check(range.begin < range.end && range.end <= source.str().size());
    if (output) *output << source.str() << ",\n";
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    test::Check(plan.pairs[i].read == OpId{2 * i});
    test::Check(plan.pairs[i].guard == OpId{2 * i + 1});
    test::Check(graph.ops[2 * i].block != graph.ops[2 * i + 1].block);
  }
  const auto checked = VerifyPhasedModuleGuards(input, graph, plan);
  test::Check(std::holds_alternative<std::uint64_t>(checked));
  const auto work = std::get<std::uint64_t>(checked);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedModuleGuards(
      input, graph, plan, {SemanticBudget{}.bytes, work})));
  test::Check(std::holds_alternative<ModuleGuardError>(VerifyPhasedModuleGuards(
      input, graph, plan, {SemanticBudget{}.bytes, work - 1})));
  test::Check(std::holds_alternative<ModuleGuards>(BuildPhasedModuleGuards(
      input, graph, {SemanticBudget{}.bytes, plan.work})));
  test::Check(std::holds_alternative<ModuleGuardError>(BuildPhasedModuleGuards(
      input, graph, {SemanticBudget{}.bytes, plan.work - 1})));
  test::Check(std::get<ModuleGuardError>(BuildPhasedModuleGuards(input, graph,
      {0, kDefaultSemanticWork})) == ModuleGuardError::kResourceLimit);
  auto changed = plan;
  changed.pairs.pop_back();
  test::Check(std::get<ModuleGuardError>(VerifyPhasedModuleGuards(input, graph, changed)) ==
      ModuleGuardError::kCoverage);
  changed = plan; ++changed.revision.value;
  test::Check(std::get<ModuleGuardError>(VerifyPhasedModuleGuards(input, graph, changed)) ==
      ModuleGuardError::kRevision);
  changed = plan; changed.pairs.front().guard = changed.pairs.front().read;
  test::Check(std::holds_alternative<ModuleGuardError>(VerifyPhasedModuleGuards(input, graph, changed)));
  auto altered = graph;
  std::get<ValueModuleRead>(altered.ops.front().operation).slot.value++;
  test::Check(std::holds_alternative<ModuleGuardError>(VerifyPhasedModuleGuards(input, altered, plan)));
  altered = graph; altered.ops[1].before_throw = LanguageEffect::kNone;
  test::Check(std::holds_alternative<ModuleGuardError>(VerifyPhasedModuleGuards(input, altered, plan)));
  altered = graph; altered.phases.front().phase = ExceptionPhase::kCommit;
  test::Check(std::holds_alternative<ModuleGuardError>(VerifyPhasedModuleGuards(input, altered, plan)));
  altered = graph; ++altered.origins.front().pc.value;
  test::Check(std::holds_alternative<ModuleGuardError>(VerifyPhasedModuleGuards(input, altered, plan)));
  if (caught && count == 1) {
    // Catching the load but not its guard cannot become one throwing read.
    // Nor may an owner inject a Language fact to bypass that missing pair.
    body.tries.front().length = body.instructions.front().decoded.size;
    const auto split = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers));
    IdfQuota split_work{kDefaultSemanticWork};
    const auto split_ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(split, frame.registers, split_work));
    const auto rejected = std::get<ExceptionModules>(ProveExceptionModules(split, split_ssa));
    test::Check(rejected.reads.empty() && rejected.loads.empty());
  }
}
void Snapshots(abc::ProfileId profile, std::ostream* output) {
  constexpr auto kGuard = abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16;
  constexpr unsigned kByteBits = 8;
  const auto op = [](abc::Opcode value) { return static_cast<std::uint8_t>(value); };
  const auto body_bytes = std::vector<std::uint8_t>{
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDEXTERNALMODULEVAR_IMM8), 0,
      op(kGuard), static_cast<std::uint16_t>(kGuard) >> kByteBits, 0, 0,
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDEXTERNALMODULEVAR_IMM8), 1,
      op(kGuard), static_cast<std::uint16_t>(kGuard) >> kByteBits, 1, 0,
      op(abc::Opcode::kRETURN), op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kRETURN)};
  auto body = test::Body(body_bytes);
  constexpr std::uint32_t kProtectedPc = 4, kProtectedBytes = 15, kHandlerPc = 19, kHandlerBytes = 3;
  abc::TryRegion region; region.offset = {kProtectedPc}; region.length = kProtectedBytes; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {kHandlerPc}; handler.length = kHandlerBytes;
  body.tries.push_back(region); body.handlers.push_back(handler);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(1, 1, {}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers));
  IdfQuota work{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, work));
  std::vector<EntryValueFact> entries;
  for (std::size_t v = 0; v < ssa.Values().size(); ++v) {
    const auto& value = ssa.Values()[v];
    const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
    if (reg && reg->value >= frame.locals && std::holds_alternative<MethodEntry>(value.origin))
      entries.push_back({{static_cast<std::uint32_t>(v)}, ValueKinds::Language()});
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
  const auto evaluation = std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(
      graph, {}, IncomingCells::kReject, {}, bindings));
  test::Check(evaluation.exceptions && !evaluation.exceptions->slots.empty());
  const auto phi_read = graph.ops.size() - 2;
  const auto phi = std::get<ValueId>(graph.uses[graph.ops[phi_read].first_use.value].operand);
  test::Check(std::holds_alternative<PhiDefinition>(graph.values[phi.value].origin));
  auto mutable_alias = evaluation;
  mutable_alias.operations[phi_read] = {EvaluationKind::kAlias};
  // Alias spelling is legal for a captured phi; replacing that identity by
  // one predecessor's module value is not. Check the semantic counterexample.
  mutable_alias.aliases[graph.ops[phi_read].result->value] = *graph.ops[guards.pairs.front().read.value].result;
  test::Check(std::get<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, mutable_alias)) == EvaluationError::kOwnership);
  // A producer used by catch staging cannot be elided even if its normal
  // consumers disappear. Preserve the old value when the next module read throws.
  auto discarded_snapshot = evaluation;
  constexpr std::size_t kInitialStore = 1;
  discarded_snapshot.operations[kInitialStore] = {EvaluationKind::kElidedPure};
  discarded_snapshot.aliases[graph.ops[kInitialStore].result->value] = *graph.ops[kInitialStore].result;
  test::Check(std::get<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, discarded_snapshot)) ==
      EvaluationError::kOwnership);
  auto changed = graph;
  bool mutated = false;
  for (std::size_t b = 0; b < evaluation.phis->bundles.size(); ++b) {
    const auto* placed = std::get_if<ThrowCopies>(&evaluation.exceptions->placements[b]);
    if (!placed || placed->operation != guards.pairs.front().guard) continue;
    const auto& bundle = evaluation.phis->bundles[b];
    test::Check(bundle.count != 0);
    test::Check(!ReplaceUse(changed, evaluation.phis->inputs[bundle.first],
        *graph.ops[guards.pairs.front().read.value].result));
    mutated = true; break;
  }
  test::Check(mutated);
  test::Check(std::get<EvaluationFailure>(BuildPhasedEvaluationPlan(
      changed, {}, IncomingCells::kReject, {}, bindings)) == EvaluationError::kOrder);
  const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
  const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
  FunctionSyntax syntax{};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.parameters.resize(1); syntax.names = {u"firstModule", u"secondModule"};
  syntax.modules = {{{{1}, {0}}, {0}, ModuleAccess::kReadOnly, NameEvidence::kSynthesized},
      {{{1}, {1}}, {1}, ModuleAccess::kReadOnly, NameEvidence::kSynthesized}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  const auto draft = std::get<ExceptionFunctionDraft>(BuildExceptionFunctionDraft(graph,
      {body, input, regions, control}, evaluation, syntax, {constants, syntax.names.size()}));
  test::Check(draft.flow&&draft.flow->original);
  test::CheckActionRegions(draft.tree,*draft.flow->original);
  const auto family = std::get<ConstructedFamily>(BuildFunctionFamily({{draft.tree, *draft.flow, {}}}, draft.tree.owner));
  std::ostringstream source;
  test::Check(std::holds_alternative<PrintedFamilyDraft>(PrintFamilyDraft(family, source)));
  test::Check(draft.tree.operations.size() == graph.ops.size());
  if (output) *output << source.str() << ",\n";
}
}  // namespace
int main(int argc, char** argv) {
  const bool emit = argc == 2 && std::string_view(argv[1]) == "--emit";
  if (emit) std::cout << "export const methods = [\n";
  for (auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                       abc::ProfileId::kDynamic1301})
    for (bool caught : {false, true})
      for (bool wide : {false, true})
        for (bool external : {false, true})
          for (auto count : {1U, 200U, 201U, 402U})
            Run(count, caught, wide, external, profile,
                emit && count == 1 && !wide && external ? &std::cout : nullptr);
  if (emit) std::cout << "];\n";
  if (emit) std::cout << "export const snapshots = [\n";
  for (auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                       abc::ProfileId::kDynamic1301}) Snapshots(profile, emit ? &std::cout : nullptr);
  if (emit) std::cout << "];\n";
  if (emit) std::cout << "export const properties = [\n";
  for (auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                       abc::ProfileId::kDynamic1301})
    for (bool caught : {false, true}) Run(1, caught, false, true, profile, emit ? &std::cout : nullptr, true);
  if (emit) std::cout << "];\n";
  for (auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                       abc::ProfileId::kDynamic1301}) {
    Run(1, true, false, true, profile, nullptr, true, true);
    for (auto count : {200U, 201U, 402U}) Run(count, true, false, true, profile, nullptr, true);
  }
}
