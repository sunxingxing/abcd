#include "../../../src/source/module_guards.h"
#include "../../../src/analysis/phased_value_import.h"
#include "abcd/analysis/exception_modules.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
void Run(std::uint32_t count, bool caught, bool wide, bool external, abc::ProfileId profile) {
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
  const auto end = static_cast<std::uint32_t>(bytes.size());
  opcode(abc::Opcode::kRETURNUNDEFINED);
  if (caught) opcode(abc::Opcode::kRETURN);
  auto body = test::Body(bytes);
  if (caught) {
    abc::TryRegion region;
    region.length = end; region.handler_count = 1;
    body.tries.push_back(region);
    abc::CatchHandler handler;
    handler.offset = {end + 1}; handler.length = 1;
    body.handlers.push_back(handler);
  }
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 0, {}));
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
}  // namespace
int main() {
  for (auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                       abc::ProfileId::kDynamic1301})
    for (bool caught : {false, true})
      for (bool wide : {false, true})
        for (bool external : {false, true})
          for (auto count : {1U, 200U, 201U, 402U}) Run(count, caught, wide, external, profile);
}
