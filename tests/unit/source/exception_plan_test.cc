#include "../../../src/source/exception_plan.h"
#include "../../../src/source/exception_control.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/tree_flow.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/family_print.h"
#include "analysis/ssa_fixture.h"

#include <sstream>
#include <string_view>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
// Pinned dynamic ISA: throw pref_none uses the throw prefix and subopcode zero.
constexpr std::uint8_t kThrowPrefix = 0xfe, kThrowCode = 0x00;
void Control(const abc::MethodBody& body, const ValueProgram& graph,
             const PhasedValueInput& input, const ExceptionPlan& regions) {
  const auto built = BuildExceptionControl(body, graph, input, regions);
  if (const auto* error = std::get_if<ExceptionControlError>(&built))
    std::cerr << "exception control error " << static_cast<unsigned>(*error) << '\n';
  test::Check(std::holds_alternative<ExceptionControl>(built));
  const auto& control = std::get<ExceptionControl>(built);
  test::Check(control.regions.size() == regions.regions.size());
  test::Check(control.units.size() <= control.skeleton.nodes.size());
  test::Check(control.arcs.size() == control.skeleton.edges.size());
  for (std::size_t r = 0; r < control.regions.size(); ++r) {
    const auto& region = control.regions[r];
    test::Check(std::get<TryControlUnit>(control.units[region.wrapper.value]).region == TryId{static_cast<std::uint32_t>(r)});
    test::Check(std::get<PhaseControlUnit>(control.units[region.handler.value]).block ==
        regions.regions[r].binding->block);
  }
  std::size_t normal = 0;
  std::vector<bool> seen(graph.edges.size());
  for (std::size_t a = 0; a < control.arcs.size(); ++a) if (const auto& arc = control.arcs[a]; arc.edge) {
    test::Check(arc.edge->value < seen.size());
    if (!seen[arc.edge->value]) { ++normal; seen[arc.edge->value] = true; }
    else {
      // The arm's real edge places its copies once; a following exit-port arc
      // carries the same origin solely for the enclosing-loop transfer.
      const auto source = control.skeleton.edges[a].owner;
      test::Check(source.value < control.units.size());
      const auto* exit = std::get_if<TryExitUnit>(&control.units[source.value]);
      test::Check(exit && exit->source == TryExitSource{*arc.edge});
    }
    test::Check(!std::holds_alternative<RaiseTransfer>(regions.transfers[arc.edge->value]));
  }
  std::size_t expected = 0;
  for (const auto& transfer : regions.transfers) expected += !std::holds_alternative<RaiseTransfer>(transfer);
  test::Check(normal == expected);
  const auto checked = VerifyExceptionControl(body, graph, input, regions, control);
  test::Check(std::holds_alternative<std::uint64_t>(checked));
  test::Check(std::holds_alternative<ControlError>(VerifyControlPlan(graph, control.skeleton)));
  auto changed = control;
  changed.regions.front().handler = changed.regions.front().body;
  test::Check(std::holds_alternative<ExceptionControlError>(
      VerifyExceptionControl(body, graph, input, regions, changed)));
  changed = control;
  changed.skeleton.nodes[changed.skeleton.entry.value].next = changed.skeleton.entry;
  test::Check(std::holds_alternative<ExceptionControlError>(
      VerifyExceptionControl(body, graph, input, regions, changed)));
  changed = control;
  changed.units.front() = TryControlUnit{{0}};
  test::Check(std::holds_alternative<ExceptionControlError>(
      VerifyExceptionControl(body, graph, input, regions, changed)));
  changed = control;
  changed.regions.front().exit = WorkBlockId{static_cast<std::uint32_t>(graph.blocks.size())};
  test::Check(std::holds_alternative<ExceptionControlError>(
      VerifyExceptionControl(body, graph, input, regions, changed)));
  changed = control;
  if (!changed.inactive_commits.empty()) {
    changed.inactive_commits.clear();
    test::Check(std::holds_alternative<ExceptionControlError>(
        VerifyExceptionControl(body, graph, input, regions, changed)));
  }
  changed = control;
  ++changed.skeleton.revision.value;
  test::Check(std::get<ExceptionControlError>(
      VerifyExceptionControl(body, graph, input, regions, changed)) == ExceptionControlError::kRevision);
  changed = control;
  if (!changed.arcs.empty()) {
    changed.arcs.front().edge = EdgeId{static_cast<std::uint32_t>(graph.edges.size())};
    test::Check(std::holds_alternative<ExceptionControlError>(
        VerifyExceptionControl(body, graph, input, regions, changed)));
  }
  const auto work = std::get<std::uint64_t>(checked);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyExceptionControl(
      body, graph, input, regions, control, {SemanticBudget{}.bytes, work})));
  test::Check(std::get<ExceptionControlError>(VerifyExceptionControl(
      body, graph, input, regions, control, {SemanticBudget{}.bytes, work - 1})) == ExceptionControlError::kWorkLimit);
  test::Check(std::holds_alternative<ExceptionControl>(BuildExceptionControl(
      body, graph, input, regions, {SemanticBudget{}.bytes, control.work})));
  test::Check(std::get<ExceptionControlError>(BuildExceptionControl(
      body, graph, input, regions, {SemanticBudget{}.bytes, control.work - 1})) == ExceptionControlError::kWorkLimit);
  test::Check(std::get<ExceptionControlError>(BuildExceptionControl(
      body, graph, input, regions, {0})) == ExceptionControlError::kResourceLimit);
}
void Nested(abc::ProfileId profile, std::uint32_t prelude, std::uint32_t depth = 2) {
  std::vector<std::uint8_t> bytes(prelude, static_cast<std::uint8_t>(abc::Opcode::kLDUNDEFINED));
  bytes.insert(bytes.end(), {static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 0});
  for (std::uint32_t r = 0; r < depth; ++r) bytes.insert(bytes.end(), {kThrowPrefix, kThrowCode});
  bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURN));
  auto body = test::Body(bytes);
  for (std::uint32_t r = 0; r < depth; ++r) {
    abc::TryRegion region;
    region.offset = {prelude}; region.length = 4 + 2 * r;
    region.first_handler = r; region.handler_count = 1;
    body.tries.push_back(region);
    abc::CatchHandler handler;
    handler.offset = {prelude + region.length}; handler.length = r + 1 == depth ? 1 : 2;
    body.handlers.push_back(handler);
  }
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {}));
  auto built = BuildExceptionFlow(body, profile, frame.registers, {}, {}, &frame);
  test::Check(std::holds_alternative<ExceptionFlow>(built));
  const auto& flow = std::get<ExceptionFlow>(built);
  IdfQuota quota{kDefaultSemanticWork};
  auto renamed = ExceptionSsa::Build(flow, frame.registers, quota);
  const auto& ssa = std::get<ExceptionSsa>(renamed);
  std::vector<EntryValueFact> entries;
  for (std::size_t i = 0; i < ssa.Values().size(); ++i)
    if (std::holds_alternative<MethodEntry>(ssa.Values()[i].origin) &&
        std::holds_alternative<abc::RegisterId>(ssa.Values()[i].slot))
      entries.push_back({{static_cast<std::uint32_t>(i)}, ValueKinds::Language()});
  auto bound = AnalyzeExceptionEnvironments(flow);
  const auto& environments = std::get<EnvironmentFlow>(bound);
  IdfQuota value_quota{kDefaultSemanticWork};
  auto propagated = ExceptionValueFlow::Build(flow, ssa, entries, value_quota);
  const auto& values = std::get<ExceptionValueFlow>(propagated);
  auto checked = VerifyExceptionInputs(flow, ssa, environments, values);
  const auto& proof = std::get<ExceptionInputProof>(checked);
  const PhasedValueInput input{flow, ssa, environments, values, proof, frame};
  auto imported = ImportPhasedValues(input);
  test::Check(std::holds_alternative<ValueProgram>(imported));
  const auto& graph = std::get<ValueProgram>(imported);
  auto planned = BuildExceptionPlan(body, graph, input);
  if (const auto* error = std::get_if<ExceptionPlanError>(&planned))
    std::cerr << "exception plan error " << static_cast<unsigned>(*error)
              << " prelude " << prelude << '\n';
  test::Check(std::holds_alternative<ExceptionPlan>(planned));
  const auto& plan = std::get<ExceptionPlan>(planned);
  Control(body, graph, input, plan);
  const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, plan));
  const auto evaluation = std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));
  FunctionSyntax syntax{};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.parameters.resize(1);
  const std::vector<ExpressionConstant> constants(graph.ops.size());
  const ExceptionTreeInput context{body, input, plan, control};
  const auto drafted = BuildExceptionFunctionDraft(graph, context, evaluation, syntax, {constants});
  if (const auto* error = std::get_if<FunctionTreeError>(&drafted))
    std::cerr << "nested exception tree error " << static_cast<unsigned>(*error) << '\n';
  test::Check(std::holds_alternative<ExceptionFunctionDraft>(drafted));
  const auto& tree = std::get<ExceptionFunctionDraft>(drafted).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(
      graph, context, evaluation, tree, {constants})));
  test::Check(plan.regions.size() == depth && plan.regions[0].binding && plan.regions[1].binding);
  for (std::uint32_t r = 0; r < depth; ++r)
    test::Check(plan.layout.source_order[r] == TryId{depth - r - 1});
  test::Check(plan.layout.regions[0].outer_dispatch == TryId{1});
  test::Check(plan.regions[0].binding->value != plan.regions[1].binding->value);
  std::size_t raises = 0, catches = 0;
  for (std::size_t e = 0; e < graph.edges.size(); ++e) {
    if (const auto* raise = std::get_if<RaiseTransfer>(&plan.transfers[e])) {
      ++raises;
      test::Check(graph.edges[e].kind == FlowEdgeKind::kException);
      test::Check(graph.edges[e].target == plan.regions[raise->region.value].binding->block);
    } else if (const auto* caught = std::get_if<CatchTransfer>(&plan.transfers[e])) {
      ++catches;
      test::Check(graph.edges[e].source == plan.regions[caught->region.value].binding->block);
      test::Check(graph.edges[e].target == plan.regions[caught->region.value].handler);
    }
  }
  test::Check(raises == depth && catches == depth);
  const auto verified = VerifyExceptionPlan(body, graph, input, plan);
  test::Check(std::holds_alternative<std::uint64_t>(verified));
  const auto work = std::get<std::uint64_t>(verified);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyExceptionPlan(
      body, graph, input, plan, {SemanticBudget{}.bytes, work})));
  test::Check(std::holds_alternative<ExceptionPlanError>(VerifyExceptionPlan(
      body, graph, input, plan, {SemanticBudget{}.bytes, work - 1})));
  test::Check(std::holds_alternative<ExceptionPlan>(BuildExceptionPlan(
      body, graph, input, {SemanticBudget{}.bytes, plan.work})));
  test::Check(std::holds_alternative<ExceptionPlanError>(BuildExceptionPlan(
      body, graph, input, {SemanticBudget{}.bytes, plan.work - 1})));
  auto changed = plan;
  changed.regions[0].binding = changed.regions[1].binding;
  test::Check(std::get<ExceptionPlanError>(VerifyExceptionPlan(body, graph, input, changed)) ==
      ExceptionPlanError::kBinding);
  changed = plan;
  changed.layout.regions[0].outer_dispatch.reset();
  test::Check(std::get<ExceptionPlanError>(VerifyExceptionPlan(body, graph, input, changed)) ==
      ExceptionPlanError::kLayout);
  changed = plan;
  changed.layout.source_order = {{0}, {1}};
  test::Check(std::get<ExceptionPlanError>(VerifyExceptionPlan(body, graph, input, changed)) ==
      ExceptionPlanError::kLayout);
  changed = plan;
  ++changed.layout.regions[0].body.end.value;
  test::Check(std::get<ExceptionPlanError>(VerifyExceptionPlan(body, graph, input, changed)) ==
      ExceptionPlanError::kLayout);
  changed = plan;
  changed.layout.scopes[prelude].reset();
  test::Check(std::get<ExceptionPlanError>(VerifyExceptionPlan(body, graph, input, changed)) ==
      ExceptionPlanError::kLayout);
  changed = plan;
  for (auto& transfer : changed.transfers)
    if (std::holds_alternative<RaiseTransfer>(transfer)) transfer = NormalTransfer{};
  test::Check(std::get<ExceptionPlanError>(VerifyExceptionPlan(body, graph, input, changed)) ==
      ExceptionPlanError::kTransfer);
  changed = plan;
  for (auto& transfer : changed.transfers)
    if (std::holds_alternative<CatchTransfer>(transfer)) transfer = NormalTransfer{};
  test::Check(std::get<ExceptionPlanError>(VerifyExceptionPlan(body, graph, input, changed)) ==
      ExceptionPlanError::kTransfer);
  changed = plan;
  ++changed.revision.value;
  test::Check(std::get<ExceptionPlanError>(VerifyExceptionPlan(body, graph, input, changed)) ==
      ExceptionPlanError::kRevision);
  changed = plan;
  changed.regions[0].handler = changed.regions[0].body;
  test::Check(std::get<ExceptionPlanError>(VerifyExceptionPlan(body, graph, input, changed)) ==
      ExceptionPlanError::kBinding);
  auto wrong_graph = graph;
  wrong_graph.origins[0].pc.value += 1;
  test::Check(std::get<ExceptionPlanError>(VerifyExceptionPlan(body, wrong_graph, input, plan)) ==
      ExceptionPlanError::kInput);
  test::Check(std::get<ExceptionPlanError>(BuildExceptionPlan(body, graph, input, {0})) ==
      ExceptionPlanError::kResourceLimit);
  test::Check(std::get<ExceptionPlanError>(BuildExceptionPlan(
      body, graph, input, {SemanticBudget{}.bytes, 0})) == ExceptionPlanError::kWorkLimit);
}
void OldLocal(abc::ProfileId profile, bool normal_handler_entry) {
  std::vector<std::uint8_t> bytes = {
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 1,
      static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0,
      static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0};
  if (normal_handler_entry) bytes.insert(bytes.end(), {static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), 2});
  else bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURN));
  const auto handler_pc = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
  auto body = test::Body(bytes);
  abc::TryRegion region;
  region.offset = {4}; region.length = handler_pc - region.offset.value; region.handler_count = 1;
  body.tries.push_back(region);
  abc::CatchHandler handler;
  handler.offset = {handler_pc}; handler.length = 3;
  body.handlers.push_back(handler);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(1, 1, {}));
  auto built = BuildExceptionFlow(body, profile, frame.registers, {}, {}, &frame);
  const auto& flow = std::get<ExceptionFlow>(built);
  IdfQuota quota{kDefaultSemanticWork};
  auto renamed = ExceptionSsa::Build(flow, frame.registers, quota);
  const auto& ssa = std::get<ExceptionSsa>(renamed);
  std::vector<EntryValueFact> entries;
  for (std::size_t v = 0; v < ssa.Values().size(); ++v) {
    const auto& value = ssa.Values()[v];
    const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
    if (std::holds_alternative<MethodEntry>(value.origin) && reg && *reg == frame.first_parameter)
      entries.push_back({{static_cast<std::uint32_t>(v)}, ValueKinds::Language()});
  }
  auto bound = AnalyzeExceptionEnvironments(flow);
  const auto& environments = std::get<EnvironmentFlow>(bound);
  IdfQuota value_quota{kDefaultSemanticWork};
  auto propagated = ExceptionValueFlow::Build(flow, ssa, entries, value_quota,
      {}, PendingExceptionValues::kLanguage);
  const auto& values = std::get<ExceptionValueFlow>(propagated);
  auto checked = VerifyExceptionInputs(flow, ssa, environments, values);
  const auto& proof = std::get<ExceptionInputProof>(checked);
  const PhasedValueInput input{flow, ssa, environments, values, proof, frame};
  auto imported = ImportPhasedValues(input);
  const auto& graph = std::get<ValueProgram>(imported);
  const auto planned = BuildExceptionPlan(body, graph, input);
  if (normal_handler_entry) {
    // A handler reachable by a normal jump needs a different control lowering;
    // it must not acquire the catch parameter's scope merely by PC identity.
    test::Check(std::get<ExceptionPlanError>(planned) == ExceptionPlanError::kLayout);
    return;
  }
  test::Check(std::holds_alternative<ExceptionPlan>(planned));
  const auto& plan = std::get<ExceptionPlan>(planned);
  Control(body, graph, input, plan);
  test::Check(plan.regions[0].binding.has_value());
  const auto loaded = std::get<ValueId>(graph.uses[graph.ops[5].first_use.value].operand);
  test::Check(loaded == graph.ops[1].result && loaded != graph.ops[3].result);
  test::Check(loaded != plan.regions[0].binding->value);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyExceptionPlan(body, graph, input, plan)));
}
void DecodedControl(const abc::MethodBody& body, abc::ProfileId profile,
                    std::optional<ExceptionControlError> expected = {}, std::ostream* output = nullptr,
                    std::optional<std::uint32_t> backedge_pc = {}) {
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers, {}, {}, &frame));
  IdfQuota quota{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
  std::vector<EntryValueFact> entries;
  for (std::size_t v = 0; v < ssa.Values().size(); ++v)
    if (std::holds_alternative<MethodEntry>(ssa.Values()[v].origin) &&
        std::holds_alternative<abc::RegisterId>(ssa.Values()[v].slot))
      entries.push_back({{static_cast<std::uint32_t>(v)}, ValueKinds::Language()});
  const auto environments = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  IdfQuota value_quota{kDefaultSemanticWork};
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(
      flow, ssa, entries, value_quota, {}, PendingExceptionValues::kLanguage));
  const auto proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, environments, values));
  const PhasedValueInput input{flow, ssa, environments, values, proof, frame};
  const auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
  if (expected) {
    test::Check(std::get<ExceptionControlError>(BuildExceptionControl(body, graph, input, regions)) == *expected);
  } else {
    Control(body, graph, input, regions);
    const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
    const auto evaluation = std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));
    std::vector<ExpressionConstant> constants(graph.ops.size());
    for (std::size_t op = 0; op < graph.ops.size(); ++op)
      if (std::holds_alternative<ValueNamedRead>(graph.ops[op].operation)) constants[op] = NameId{0};
    FunctionSyntax syntax{};
    syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    syntax.names = {u"field"}; syntax.parameters.resize(frame.parameters);
    const ExceptionTreeInput context{body, input, regions, control};
    const auto built = BuildExceptionFunctionDraft(graph, context, evaluation, syntax, {constants, 1});
    if (const auto* error = std::get_if<FunctionTreeError>(&built))
      std::cerr << "control draft error " << static_cast<unsigned>(*error) << '\n';
    test::Check(std::holds_alternative<ExceptionFunctionDraft>(built));
    const auto& tree = std::get<ExceptionFunctionDraft>(built).tree;
    if (backedge_pc) {
      bool saw_continue = false;
      for (const auto& statement : tree.statements) {
        if (!std::holds_alternative<ContinueStmt>(statement.statement)) continue;
        saw_continue = true;
        const auto edge = std::get<EdgeOrigin>(tree.origins[statement.origin.value]).edge;
        test::Check(edge.value < graph.edges.size());
        // The backedge leaves the Commit phase of the original JMP, not the
        // numerically equal projected arc after exception edges were removed.
        const auto phase = graph.phases[graph.edges[edge.value].source.value];
        test::Check(phase.phase == ExceptionPhase::kCommit);
        test::Check(phase.operation.has_value());
        test::Check(graph.origins[graph.ops[phase.operation->value].origin.value].pc.value == *backedge_pc);
      }
      test::Check(saw_continue);
    }
    const auto checked_tree = VerifyExceptionTreeFlow(graph, context, evaluation, tree);
    if (const auto* error = std::get_if<TreeFlowError>(&checked_tree))
      std::cerr << "exception region flow error " << static_cast<unsigned>(*error) << '\n';
    test::Check(std::holds_alternative<TreeFlow>(checked_tree));
    const auto checked_semantics = VerifyExceptionTreeSemantics(graph, context, evaluation, tree, {constants, 1});
    if (const auto* error = std::get_if<TreeSemanticError>(&checked_semantics))
      std::cerr << "exception region semantics error " << static_cast<unsigned>(*error) << '\n';
    test::Check(std::holds_alternative<TreeSemantics>(checked_semantics));
    const auto& checked_flow = std::get<TreeFlow>(checked_tree);
    std::vector<bool> parameters(tree.symbols.size());
    for (const auto symbol : checked_flow.catch_parameters)
      if (symbol) parameters[symbol->value] = true;
    for (std::size_t i = 0; i < tree.expressions.size(); ++i) {
      const auto* symbol = std::get_if<SymbolExpr>(&tree.expressions[i].expression);
      if (!symbol || !parameters[symbol->symbol.value]) continue;
      auto changed = tree;
      changed.expressions[i].expression = SymbolExpr{tree.parameters[0].symbol};
      test::Check(std::holds_alternative<TreeFlow>(VerifyExceptionTreeFlow(graph, context, evaluation, changed)));
      test::Check(std::get<TreeSemanticError>(VerifyExceptionTreeSemantics(
          graph, context, evaluation, changed, {constants, 1})) == TreeSemanticError::kOperand);
      break;
    }
    test::Check(tree.operations.size() == graph.ops.size());
    const auto family = std::get<ConstructedFamily>(BuildFunctionFamily({{tree, {}, {}}}, tree.owner));
    std::ostringstream text;
    const auto printed = PrintFamilyDraft(family, text);
    test::Check(std::holds_alternative<PrintedFamilyDraft>(printed));
    const auto& positions = std::get<PrintedFamilyDraft>(printed).members[0];
    const auto source_size = text.str().size();
    for (const auto& placement : tree.operations) {
      if (const auto* statement = std::get_if<StmtId>(&placement.node)) {
        const auto range = positions.statements[statement->value];
        test::Check(range.begin < range.end && range.end <= source_size);
        const auto origin = tree.statements[statement->value].origin;
        test::Check(std::get<OriginalSite>(tree.origins[origin.value]).pc ==
            graph.origins[graph.ops[placement.operation.value].origin.value].pc);
      } else if (const auto* anchor = std::get_if<AnchorId>(&placement.node)) {
        test::Check(positions.anchors[anchor->value] <= source_size);
      }
    }
    if (output) *output << text.str() << ",\n";
  }
}
void BranchJoin(abc::ProfileId profile, std::ostream* output = nullptr) {
  // Protected diamond rejoins at PC14, then body and handler both continue at
  // PC19. The getter has a raising edge, not a third source-level branch arm.
  std::vector<std::uint8_t> bytes = {
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0,
      static_cast<std::uint8_t>(abc::Opcode::kJEQZ_IMM8), 5,
      static_cast<std::uint8_t>(abc::Opcode::kLDTRUE),
      static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), 5,
      static_cast<std::uint8_t>(abc::Opcode::kLDFALSE),
      static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), 2,
      static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), 5,
      static_cast<std::uint8_t>(abc::Opcode::kLDUNDEFINED),
      static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), 2,
      static_cast<std::uint8_t>(abc::Opcode::kRETURN)};
  auto body = test::Body(bytes);
  abc::TryRegion region;
  region.length = 16; region.handler_count = 1;
  body.tries.push_back(region);
  abc::CatchHandler handler;
  handler.offset = {16}; handler.length = 3;
  body.handlers.push_back(handler);
  DecodedControl(body, profile, {}, output);
  body.tries[0].length = 14; // The effect-free gap jump keeps its original PC/edges.
  DecodedControl(body, profile, {}, output);
  {
    // A jump shared by protected and handler paths belongs to neither arm.
    auto shared = bytes;
    constexpr std::size_t kHandlerJumpOperand = 18;
    constexpr std::int8_t kToSharedGap = -3;
    shared[kHandlerJumpOperand] = static_cast<std::uint8_t>(kToSharedGap);
    auto joined = body;
    joined.instructions = test::Body(shared).instructions;
    DecodedControl(joined, profile);
  }
  for (const std::uint32_t count : {1U, 200U, 201U, 402U}) {
    auto corridor = bytes;
    constexpr std::uint32_t kJumpBytes = 2, kGapPc = 14, kHandlerPc = 16;
    std::vector<std::uint8_t> jumps;
    for (std::uint32_t i = 0; i < count; ++i)
      jumps.insert(jumps.end(), {static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), kJumpBytes});
    corridor.insert(corridor.begin() + kGapPc, jumps.begin(), jumps.end());
    auto extended = body;
    extended.instructions = test::Body(corridor).instructions;
    extended.handlers[0].offset = {kHandlerPc + count * kJumpBytes};
    DecodedControl(extended, profile);
  }
  {
    // A getter in the gap is outside the original catch. Moving it into the
    // protected body would change thrown-value routing, even on a single path.
    auto effectful = bytes;
    constexpr std::uint32_t kGapPc = 14, kReadBytes = 4;
    effectful.insert(effectful.begin() + kGapPc,
        {static_cast<std::uint8_t>(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0});
    auto outside = body;
    outside.instructions = test::Body(effectful).instructions;
    outside.handlers[0].offset.value += kReadBytes;
    DecodedControl(outside, profile, ExceptionControlError::kMultipleExits);
  }
  body.tries[0].length = 16;
  constexpr std::size_t kBackedgeOperand = 13;
  constexpr std::int8_t kBackedgeToPredicate = -12;
  bytes[kBackedgeOperand] = static_cast<std::uint8_t>(kBackedgeToPredicate);
  body.instructions = test::Body(bytes).instructions;
  DecodedControl(body, profile, {}, output, 12);
}
void HandlerLoop(abc::ProfileId profile, std::ostream* output = nullptr) {
  // First getter is protected; later reads in the handler's loop are not.
  const std::vector<std::uint8_t> bytes = {
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0,
      static_cast<std::uint8_t>(abc::Opcode::kRETURN),
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0,
      static_cast<std::uint8_t>(abc::Opcode::kJEQZ_IMM8), 3,
      static_cast<std::uint8_t>(abc::Opcode::kRETURN),
      static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-9)};
  auto body = test::Body(bytes);
  abc::TryRegion region; region.length = 6; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {7}; handler.length = 11;
  body.tries = {region}; body.handlers = {handler};
  DecodedControl(body, profile, {}, output, 16);
}
void HandlerNestedTry(abc::ProfileId profile, std::ostream* output = nullptr) {
  const std::vector<std::uint8_t> bytes = {
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 0, kThrowPrefix, kThrowCode,
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 0, kThrowPrefix, kThrowCode,
      static_cast<std::uint8_t>(abc::Opcode::kRETURN)};
  auto body = test::Body(bytes);
  abc::TryRegion outer; outer.length = 4; outer.handler_count = 1;
  abc::TryRegion inner; inner.offset = {4}; inner.length = 4; inner.first_handler = 1; inner.handler_count = 1;
  body.tries = {outer, inner};
  abc::CatchHandler first; first.offset = {4}; first.length = 5;
  abc::CatchHandler second; second.offset = {8}; second.length = 1;
  body.handlers = {first, second};
  DecodedControl(body, profile, {}, output);
}
void RetryTry(abc::ProfileId profile, std::ostream* output = nullptr) {
  const std::vector<std::uint8_t> bytes = {
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0,
      static_cast<std::uint8_t>(abc::Opcode::kRETURN),
      static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-7)};
  auto body = test::Body(bytes);
  abc::TryRegion region; region.length = 7; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {7}; handler.length = 2;
  body.tries = {region}; body.handlers = {handler};
  DecodedControl(body, profile, {}, output);
}
}  // namespace
int main(int argc, char** argv) {
  const bool emit = argc == 2 && std::string_view(argv[1]) == "--emit";
  if (emit) std::cout << "export const regions = [\n";
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                            abc::ProfileId::kDynamic1301}) {
    for (const std::uint32_t size : {0U, 1U, 200U, 201U, 402U}) Nested(profile, size);
    OldLocal(profile, false);
    OldLocal(profile, true);
    BranchJoin(profile, emit ? &std::cout : nullptr);
    HandlerLoop(profile, emit ? &std::cout : nullptr);
    RetryTry(profile, emit ? &std::cout : nullptr);
    HandlerNestedTry(profile, emit ? &std::cout : nullptr);
  }
  for (const std::uint32_t depth : {200U, 201U, 402U})
    Nested(abc::ProfileId::kDynamic1301, 1, depth);
  if (emit) std::cout << "];\n";
}
