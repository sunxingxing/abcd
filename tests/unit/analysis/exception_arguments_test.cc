#include "abcd/analysis/exception_inputs.h"

#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
constexpr auto kProfile = abc::ProfileId::kDynamic1301;

abc::MethodBody SnapshotBody() {
  // Pinned v7: getunmappedargs; return; catch: return.
  auto body = test::Body({0x6c, 0x64, 0x64});
  abc::TryRegion region;
  region.length = 1;
  region.handler_count = 1;
  body.tries.push_back(region);
  abc::CatchHandler handler;
  handler.offset = {2};
  handler.length = 1;
  body.handlers.push_back(handler);
  return body;
}

ExceptionNodeId Phase(const ExceptionFlow& flow, abc::CodeOffset pc,
                      ExceptionPhase phase) {
  for (std::size_t i = 0; i < flow.nodes.size(); ++i) {
    const auto& node = flow.nodes[i];
    if (node.phase == phase && flow.instructions[node.instruction.value].pc == pc)
      return {static_cast<std::uint32_t>(i)};
  }
  test::Check(false);
  return {};
}

void CheckSnapshot(std::uint32_t parameters) {
  constexpr std::uint32_t kLocals = 2;
  constexpr std::uint32_t kHidden = 3;  // function, new.target, this ABI slots.
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
      kLocals, parameters + kHidden, {true, true, true, true}));
  const auto body = SnapshotBody();
  const auto built = BuildExceptionFlow(body, kProfile, frame.registers, {}, {}, &frame);
  test::Check(std::holds_alternative<ExceptionFlow>(built));
  const auto& flow = std::get<ExceptionFlow>(built);
  test::Check(flow.frame == frame);
  const auto before = Phase(flow, {0}, ExceptionPhase::kBefore);
  const auto commit = Phase(flow, {0}, ExceptionPhase::kCommit);
  const auto caught = Phase(flow, {2}, ExceptionPhase::kCatch);
  const auto normal_return = Phase(flow, {1}, ExceptionPhase::kBefore);
  const auto handler_return = Phase(flow, {2}, ExceptionPhase::kBefore);
  IdfQuota quota{kDefaultSemanticWork};
  auto renamed = ExceptionSsa::Build(flow, frame.registers, quota);
  test::Check(std::holds_alternative<ExceptionSsa>(renamed));
  const auto& ssa = std::get<ExceptionSsa>(renamed);
  const auto reads = ssa.Reads(before);
  test::Check(reads.size() == parameters);
  std::vector<EntryValueFact> entries;
  for (std::uint32_t i = 0; i < parameters; ++i) {
    test::Check(reads[i].ordinal == i);
    const auto& value = ssa.Values()[reads[i].value.value];
    test::Check(std::get<abc::RegisterId>(value.slot).value == frame.first_parameter.value + i);
    test::Check(std::holds_alternative<MethodEntry>(value.origin));
    entries.push_back({reads[i].value, ValueKinds::Language()});
  }
  test::Check(!ssa.Write(before));
  test::Check(ssa.Reads(commit).empty());
  test::Check(ssa.Write(commit) && ssa.Write(caught));
  test::Check(ssa.Reads(normal_return)[0].value == *ssa.Write(commit));
  test::Check(ssa.Reads(handler_return)[0].value == *ssa.Write(caught));
  test::Check(ssa.Write(commit) != ssa.Write(caught));
  auto bound = AnalyzeExceptionEnvironments(flow);
  test::Check(std::holds_alternative<EnvironmentFlow>(bound));
  const auto& environments = std::get<EnvironmentFlow>(bound);
  IdfQuota value_quota{kDefaultSemanticWork};
  auto propagated = ExceptionValueFlow::Build(flow, ssa, entries, value_quota,
      {}, PendingExceptionValues::kLanguage);
  test::Check(std::holds_alternative<ExceptionValueFlow>(propagated));
  const auto& values = std::get<ExceptionValueFlow>(propagated);
  test::Check(std::get<ExceptionInputFailure>(VerifyExceptionInputs(
      flow, ssa, environments, values)).kind == ExceptionInputError::kArgumentOwner);
  const ArgumentFrameFact invocation{frame};
  const auto checked = VerifyExceptionInputs(flow, ssa, environments, values,
      kDefaultSemanticWork, &invocation);
  test::Check(std::holds_alternative<ExceptionInputProof>(checked));
  const auto& proof = std::get<ExceptionInputProof>(checked);
  test::Check(proof.arguments == invocation);
  test::Check(proof.reads == parameters + 2);  // plus the two return paths.
  test::Check(std::holds_alternative<ExceptionInputProof>(VerifyExceptionInputs(
      flow, ssa, environments, values, proof.work_units, &invocation)));
  test::Check(std::get<ExceptionInputFailure>(VerifyExceptionInputs(flow, ssa,
      environments, values, proof.work_units - 1, &invocation)).kind ==
      ExceptionInputError::kResourceLimit);
  auto wrong = invocation;
  wrong.frame.extra_arguments = false;
  test::Check(std::get<ExceptionInputFailure>(VerifyExceptionInputs(flow, ssa,
      environments, values, kDefaultSemanticWork, &wrong)).kind ==
      ExceptionInputError::kArgumentOwner);
  auto changed = flow;
  changed.frame.reset();
  test::Check(std::get<ExceptionInputFailure>(VerifyExceptionInputs(changed, ssa,
      environments, values, kDefaultSemanticWork, &invocation)).kind ==
      ExceptionInputError::kArgumentOwner);
  changed = flow;
  changed.instructions[0].effects.before_throw = LanguageEffect::kNone;
  test::Check(std::get<ExceptionInputFailure>(VerifyExceptionInputs(changed, ssa,
      environments, values, kDefaultSemanticWork, &invocation)).kind ==
      ExceptionInputError::kMismatchedAnalysis);
  changed = flow;
  for (auto& edge : changed.nodes[before.value].successors)
    if (edge.kind == ExceptionArcKind::kNormal) edge.target = caught;
  test::Check(std::get<ExceptionInputFailure>(VerifyExceptionInputs(changed, ssa,
      environments, values, kDefaultSemanticWork, &invocation)).kind ==
      ExceptionInputError::kMismatchedAnalysis);
  if (parameters != 0) {
    entries[0].kinds = ValueKinds::Hole();
    IdfQuota unknown_quota{kDefaultSemanticWork};
    auto internal = ExceptionValueFlow::Build(flow, ssa, entries, unknown_quota,
        {}, PendingExceptionValues::kLanguage);
    test::Check(std::holds_alternative<ExceptionValueFlow>(internal));
    test::Check(std::get<ExceptionInputFailure>(VerifyExceptionInputs(flow, ssa,
        environments, std::get<ExceptionValueFlow>(internal), kDefaultSemanticWork,
        &invocation)).kind == ExceptionInputError::kInternalValue);
  }
  test::Check(std::get<SemanticFailure>(BuildExceptionFlow(body, kProfile,
      frame.registers)).kind == SemanticError::kUnsupportedInstruction);
  test::Check(std::get<SemanticFailure>(BuildExceptionFlow(body, kProfile,
      frame.registers + 1, {}, {}, &frame)).kind == SemanticError::kMalformedInstruction);
  auto malformed = frame;
  ++malformed.first_parameter.value;
  test::Check(std::get<SemanticFailure>(BuildExceptionFlow(body, kProfile,
      frame.registers, {}, {}, &malformed)).kind == SemanticError::kMalformedInstruction);
}

void LexicalState() {
  // Allocate an environment; snapshot; pop on success; return. The catch
  // returns without executing the pop, so its environment must stay local.
  auto body = test::Body({0x09, 0x01, 0x6c, 0x69, 0x64, 0x64});
  abc::TryRegion region;
  region.offset = {2};
  region.length = 1;
  region.handler_count = 1;
  body.tries.push_back(region);
  abc::CatchHandler handler;
  handler.offset = {5};
  handler.length = 1;
  body.handlers.push_back(handler);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
      0, 0, {false, false, true, false}));
  auto built = BuildExceptionFlow(body, kProfile, frame.registers, {}, {}, &frame);
  test::Check(std::holds_alternative<ExceptionFlow>(built));
  const auto& flow = std::get<ExceptionFlow>(built);
  auto bound = AnalyzeExceptionEnvironments(flow);
  test::Check(std::holds_alternative<EnvironmentFlow>(bound));
  const auto& environments = std::get<EnvironmentFlow>(bound);
  const auto snapshot = Phase(flow, {2}, ExceptionPhase::kBefore);
  const auto returned = Phase(flow, {4}, ExceptionPhase::kBefore);
  const auto caught = Phase(flow, {5}, ExceptionPhase::kCatch);
  test::Check(environments.before[snapshot.value] && environments.before[returned.value]);
  test::Check(std::holds_alternative<EnvironmentId>(*environments.before[snapshot.value]));
  test::Check(std::holds_alternative<CapturedDepth>(*environments.before[returned.value]));
  test::Check(environments.before[caught.value] == environments.before[snapshot.value]);
  IdfQuota quota{kDefaultSemanticWork};
  auto renamed = ExceptionSsa::Build(flow, frame.registers, quota);
  test::Check(std::holds_alternative<ExceptionSsa>(renamed));
  const auto& ssa = std::get<ExceptionSsa>(renamed);
  IdfQuota value_quota{kDefaultSemanticWork};
  auto propagated = ExceptionValueFlow::Build(flow, ssa, {}, value_quota,
      {}, PendingExceptionValues::kLanguage);
  test::Check(std::holds_alternative<ExceptionValueFlow>(propagated));
  const auto& values = std::get<ExceptionValueFlow>(propagated);
  const ArgumentFrameFact invocation{frame};
  test::Check(std::holds_alternative<ExceptionInputProof>(VerifyExceptionInputs(
      flow, ssa, environments, values, kDefaultSemanticWork, &invocation)));
  test::Check(std::get<ExceptionInputFailure>(VerifyExceptionInputs(
      flow, ssa, environments, values)).kind == ExceptionInputError::kArgumentOwner);
}
}  // namespace

int main() {
  for (const auto parameters : {0u, 1u, 200u, 201u, 402u})
    CheckSnapshot(parameters);
  LexicalState();
}
