#include "../../../src/source/exception_plan.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
// Pinned dynamic ISA: throw pref_none uses the throw prefix and subopcode zero.
constexpr std::uint8_t kThrowPrefix = 0xfe, kThrowCode = 0x00;
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
  test::Check(plan.regions[0].binding.has_value());
  const auto loaded = std::get<ValueId>(graph.uses[graph.ops[5].first_use.value].operand);
  test::Check(loaded == graph.ops[1].result && loaded != graph.ops[3].result);
  test::Check(loaded != plan.regions[0].binding->value);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyExceptionPlan(body, graph, input, plan)));
}
}  // namespace
int main() {
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                            abc::ProfileId::kDynamic1301}) {
    for (const std::uint32_t size : {0U, 1U, 200U, 201U, 402U}) Nested(profile, size);
    OldLocal(profile, false);
    OldLocal(profile, true);
  }
  for (const std::uint32_t depth : {200U, 201U, 402U})
    Nested(abc::ProfileId::kDynamic1301, 1, depth);
}
