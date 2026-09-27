#include "../../../src/analysis/phased_value_import.h"
#include "abcd/analysis/value_environment.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
enum class Exit { kReturn, kThrow };
void Run(abc::ProfileId profile, std::size_t count = 1, Exit exit = Exit::kReturn) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr std::uint8_t kParameter = 3;
  const std::vector<std::uint8_t> sequence{op(abc::Opcode::kNEWLEXENV_IMM8), 1,
      op(abc::Opcode::kLDA_V8), kParameter, op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
      op(abc::Opcode::kPOPLEXENV)};
  std::vector<std::uint8_t> bytes;
  for (std::size_t i = 0; i < count; ++i) bytes.insert(bytes.end(), sequence.begin(), sequence.end());
  if (exit == Exit::kReturn) bytes.push_back(op(abc::Opcode::kRETURNUNDEFINED));
  else {
    constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
    bytes.push_back(static_cast<std::uint8_t>(kThrow));
    bytes.push_back(static_cast<std::uint8_t>(kThrow >> 8));
  }
  const auto catch_pc = static_cast<std::uint32_t>(bytes.size());
  bytes.push_back(op(abc::Opcode::kRETURN));
  auto body = test::Body(bytes);
  abc::TryRegion region; region.length = 2; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {catch_pc}; handler.length = 1;
  body.tries.push_back(region); body.handlers.push_back(handler);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 4, {true, true, false, true}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers));
  IdfQuota ssa_work{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, ssa_work));
  std::vector<EntryValueFact> entries;
  for (std::size_t i = 0; i < ssa.Values().size(); ++i)
    if (std::holds_alternative<MethodEntry>(ssa.Values()[i].origin) &&
        std::holds_alternative<abc::RegisterId>(ssa.Values()[i].slot))
      entries.push_back({{static_cast<std::uint32_t>(i)}, ValueKinds::Language()});
  const auto env = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
      value_work, {}, PendingExceptionValues::kLanguage));
  const auto proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, env, values));
  const auto graph = std::get<ValueProgram>(ImportPhasedValues({flow, ssa, env, values, proof, frame}));
  auto built = BuildPhasedValueEnvironments(graph);
  test::Check(std::holds_alternative<ValueEnvironments>(built));
  auto& result = std::get<ValueEnvironments>(built);
  const EnvironmentRef captured{CapturedDepth{0}};
  const EnvironmentRef local{EnvironmentId{0}};
  test::Check(result.phase_entries.size() == graph.blocks.size());
  test::Check(result.flow.allocations.size() == count && result.flow.bindings.size() == count);
  test::Check(result.flow.before[0] == captured && result.flow.before[2] == local &&
      result.flow.before[3] == local && result.flow.before.back() == captured);
  test::Check(result.flow.allocations[0].instruction == 0 && result.flow.bindings[0].instruction == 2);
  test::Check(result.flow.bindings[0].binding.environment == local);
  const auto verify = [&] { return VerifyPhasedValueEnvironments(graph, result); };
  const auto checked = verify();
  test::Check(std::holds_alternative<std::uint64_t>(checked));
  test::Check(std::holds_alternative<ValueEnvironmentError>(BuildValueEnvironments(graph)));
  test::Check(std::holds_alternative<ValueEnvironmentError>(VerifyValueEnvironments(graph, result)));
  SemanticBudget budget;
  budget.work = result.work;
  test::Check(std::holds_alternative<ValueEnvironments>(BuildPhasedValueEnvironments(graph, budget)));
  --budget.work;
  test::Check(std::get<ValueEnvironmentError>(BuildPhasedValueEnvironments(graph, budget)) ==
      ValueEnvironmentError::kWorkLimit);
  budget = {}; budget.bytes = 0;
  test::Check(std::get<ValueEnvironmentError>(BuildPhasedValueEnvironments(graph, budget)) ==
      ValueEnvironmentError::kResourceLimit);
  budget = {}; budget.work = std::get<std::uint64_t>(checked);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedValueEnvironments(graph, result, budget)));
  --budget.work;
  test::Check(std::get<ValueEnvironmentError>(VerifyPhasedValueEnvironments(graph, result, budget)) ==
      ValueEnvironmentError::kWorkLimit);
  // Deliberately corrupt independently supplied certificates, not the input CFG.
  ++result.revision.value;
  test::Check(std::get<ValueEnvironmentError>(verify()) == ValueEnvironmentError::kRevision);
  --result.revision.value;
  ++result.cfg_revision.value;
  test::Check(std::get<ValueEnvironmentError>(verify()) == ValueEnvironmentError::kRevision);
  --result.cfg_revision.value;
  auto phases = std::move(result.phase_entries);
  result.phase_entries.clear();
  test::Check(std::get<ValueEnvironmentError>(verify()) == ValueEnvironmentError::kState);
  result.phase_entries = std::move(phases);
  result.flow.before[2] = captured;
  test::Check(std::get<ValueEnvironmentError>(verify()) == ValueEnvironmentError::kState);
  result.flow.before[2] = local;
  result.flow.bindings[0].binding.slot.value = 1;
  test::Check(std::get<ValueEnvironmentError>(verify()) == ValueEnvironmentError::kBinding);
  result.flow.bindings[0].binding.slot.value = 0;
  result.flow.allocations[0].instruction = 1;
  test::Check(std::get<ValueEnvironmentError>(verify()) == ValueEnvironmentError::kAllocation);
  result.flow.allocations[0].instruction = 0;
  for (std::size_t b = 0; b < graph.phases.size(); ++b) {
    const auto& phase = graph.phases[b];
    if (phase.phase != ExceptionPhase::kCatch &&
        !(phase.phase == ExceptionPhase::kCommit && phase.operation == OpId{0})) continue;
    test::Check(result.phase_entries[b] == captured);
    result.phase_entries[b] = local;
    test::Check(std::get<ValueEnvironmentError>(verify()) == ValueEnvironmentError::kState);
    result.phase_entries[b] = captured;
  }
  test::Check(std::holds_alternative<std::uint64_t>(verify()));
  for (std::size_t b = 0; b < graph.phases.size(); ++b) {
    const auto& phase = graph.phases[b];
    if (phase.phase != ExceptionPhase::kCommit || !phase.operation ||
        !std::holds_alternative<ValueThrow>(graph.ops[phase.operation->value].operation)) continue;
    test::Check(!result.phase_entries[b]);
    result.phase_entries[b] = captured;
    test::Check(std::get<ValueEnvironmentError>(verify()) == ValueEnvironmentError::kState);
    result.phase_entries[b].reset();
  }
}
}  // namespace
int main() {
  for (const auto profile : {abcd::abc::ProfileId::kDynamic1202, abcd::abc::ProfileId::kDynamic1206,
                            abcd::abc::ProfileId::kDynamic1301}) {
    Run(profile);
    Run(profile, 1, Exit::kThrow);
  }
  for (const std::size_t count : {200, 201, 402}) Run(abcd::abc::ProfileId::kDynamic1301, count);
}
