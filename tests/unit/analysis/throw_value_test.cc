#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/analysis/value_flow.h"
#include "../../../src/analysis/value_edits.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
constexpr std::uint8_t kThrowPrefix = 0xfe, kThrowCode = 0x00;
void ThrowParameter(abc::ProfileId profile, bool caught, bool unreachable = false,
                    std::uint32_t prelude = 0) {
  auto bytes = std::vector<std::uint8_t>(prelude, static_cast<std::uint8_t>(abc::Opcode::kLDUNDEFINED));
  bytes.insert(bytes.end(), {static_cast<std::uint8_t>(abc::Opcode::kLDA_V8),
      0, kThrowPrefix, kThrowCode});
  const std::uint32_t throw_index = prelude + 1;
  if (caught) bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURN));
  if (unreachable) bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURNUNDEFINED));
  auto body = test::Body(bytes);
  if (caught) {
    abc::TryRegion region;
    region.offset = {prelude + 2}; region.length = 2; region.handler_count = 1;
    body.tries.push_back(region);
    abc::CatchHandler handler;
    handler.offset = {prelude + 4}; handler.length = 1;
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
  const auto bound = AnalyzeExceptionEnvironments(flow);
  const auto& environments = std::get<EnvironmentFlow>(bound);
  IdfQuota value_quota{kDefaultSemanticWork};
  auto propagated = ExceptionValueFlow::Build(flow, ssa, entries, value_quota);
  const auto& values = std::get<ExceptionValueFlow>(propagated);
  const auto checked = VerifyExceptionInputs(flow, ssa, environments, values);
  test::Check(std::holds_alternative<ExceptionInputProof>(checked));
  const auto& proof = std::get<ExceptionInputProof>(checked);
  const PhasedValueInput input{flow, ssa, environments, values, proof, frame};
  const auto imported = ImportPhasedValues(input);
  if (unreachable) {
    test::Check(std::get<ValueImportFailure>(imported).kind == ValueImportError::kUnreachableFlow);
    return;
  }
  test::Check(std::holds_alternative<ValueProgram>(imported));
  const auto& graph = std::get<ValueProgram>(imported);
  test::Check(graph.ops.size() == body.instructions.size());
  test::Check(graph.phases.size() == flow.nodes.size());
  test::Check(graph.ops[throw_index].exceptional == ExceptionalCompletion::kAlwaysThrow);
  test::Check(std::holds_alternative<ValueThrow>(graph.ops[throw_index].operation));
  test::Check(!CanErasePureOp(graph.ops[throw_index]));
  test::Check(!graph.ops[throw_index].result && graph.ops[throw_index].use_count == 1);
  test::Check(graph.exceptional_exits.size() == 1);
  test::Check(graph.exceptional_exits.front().handler.has_value() == caught);
  test::Check(std::holds_alternative<PhasedImportProof>(VerifyImportedPhases(graph, input)));
  std::size_t dead_commits = 0;
  for (std::size_t i = 0; i < flow.nodes.size(); ++i) {
    if (environments.before[i]) continue;
    ++dead_commits;
    test::Check(graph.phases[i].phase == ExceptionPhase::kCommit &&
        graph.phases[i].operation == OpId{throw_index});
    test::Check(graph.blocks[i].op_count == 0 && graph.blocks[i].edge_count == 0);
  }
  test::Check(dead_commits == 1);
  auto changed = graph;
  changed.ops[throw_index].exceptional = ExceptionalCompletion::kMayThrow;
  test::Check(std::holds_alternative<ValueAvailabilityError>(VerifyPhasedValueFlow(changed)));
  changed = graph;
  changed.ops[throw_index].operation = ValueReturn{};
  test::Check(std::holds_alternative<ValueAvailabilityError>(VerifyPhasedValueFlow(changed)));
  auto wrong = flow;
  wrong.nodes[graph.ops[throw_index].block.value].exception->exception = PendingException{};
  const PhasedValueInput mismatch{wrong, ssa, environments, values, proof, frame};
  test::Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(graph, mismatch)));
  entries.front().kinds = ValueKinds::Hole();
  IdfQuota internal_quota{kDefaultSemanticWork};
  auto internal = ExceptionValueFlow::Build(flow, ssa, entries, internal_quota,
      {}, PendingExceptionValues::kLanguage);
  test::Check(std::get<ExceptionInputFailure>(VerifyExceptionInputs(flow, ssa, environments,
      std::get<ExceptionValueFlow>(internal))).kind == ExceptionInputError::kInternalValue);
  test::Check(std::holds_alternative<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, environments,
      std::get<ExceptionValueFlow>(internal), kDefaultSemanticWork, nullptr,
      ExceptionInputDomain::kHoleTransport)));
  for (const auto kinds : {ValueKinds::Environment(), ValueKinds::OtherInternal(), ValueKinds::Unknown()}) {
    entries.front().kinds = kinds;
    IdfQuota transport_quota{kDefaultSemanticWork};
    auto transport = ExceptionValueFlow::Build(flow, ssa, entries, transport_quota,
        {}, PendingExceptionValues::kLanguage);
    test::Check(std::get<ExceptionInputFailure>(VerifyExceptionInputs(flow, ssa, environments,
        std::get<ExceptionValueFlow>(transport), kDefaultSemanticWork, nullptr,
        ExceptionInputDomain::kHoleTransport)).kind == ExceptionInputError::kInternalValue);
  }
}
}
int main() {
  for (const auto profile : {abcd::abc::ProfileId::kDynamic1202,
      abcd::abc::ProfileId::kDynamic1206, abcd::abc::ProfileId::kDynamic1301}) {
    ThrowParameter(profile, false);
    ThrowParameter(profile, true);
    ThrowParameter(profile, false, true);
  }
  for (const std::uint32_t count : {200U, 201U, 402U})
    ThrowParameter(abcd::abc::ProfileId::kDynamic1301, true, false, count);
}
