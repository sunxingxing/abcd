#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/analysis/value_flow.h"
#include "analysis/ssa_fixture.h"

#include <algorithm>

namespace {
using namespace abcd;
using namespace abcd::analysis;
enum class SnapshotShape { kSeparateReturns, kHandlerJoin, kLexicalPrelude, kDeadCode, kDeadHandler, kDeadLoop };
void Snapshot(std::uint32_t parameters, abc::ProfileId profile,
              SnapshotShape shape = SnapshotShape::kSeparateReturns) {
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
      0, parameters, {false, false, true, false}));
  auto body = test::Body({static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS),
      static_cast<std::uint8_t>(abc::Opcode::kRETURN),
      static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
  abc::CodeOffset snapshot_pc{0};
  abc::CodeOffset handler_pc{2};
  std::size_t snapshot_index = 0;
  if (shape == SnapshotShape::kHandlerJoin) {
    body = test::Body({static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS),
        static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), 2,
        static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
    handler_pc = {3};
  } else if (shape == SnapshotShape::kLexicalPrelude) {
    body = test::Body({static_cast<std::uint8_t>(abc::Opcode::kNEWLEXENV_IMM8), 1,
        static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS),
        static_cast<std::uint8_t>(abc::Opcode::kPOPLEXENV),
        static_cast<std::uint8_t>(abc::Opcode::kRETURN),
        static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
    snapshot_pc = {2}; handler_pc = {5}; snapshot_index = 1;
  } else if (shape == SnapshotShape::kDeadHandler) {
    body = test::Body({static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS),
        static_cast<std::uint8_t>(abc::Opcode::kRETURN),
        static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS),
        static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
    handler_pc = {3};
  } else if (shape == SnapshotShape::kDeadLoop) {
    body = test::Body({static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS),
        static_cast<std::uint8_t>(abc::Opcode::kRETURN),
        static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), 0,
        static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
    handler_pc = {4};
  } else if (shape == SnapshotShape::kDeadCode) {
    // Dead reads/writes and a dead predecessor of the live handler must not
    // acquire SSA definitions, operands, edges or catch payloads.
    std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS),
        static_cast<std::uint8_t>(abc::Opcode::kRETURN)};
    for (std::uint32_t i = 0; i < parameters; ++i) {
      bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kLDA_V8)); bytes.push_back(0);
      bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kSTA_V8)); bytes.push_back(0);
      bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS));
    }
    bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8)); bytes.push_back(2);
    handler_pc = {static_cast<std::uint32_t>(bytes.size())};
    bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURN));
    body = test::Body(bytes);
  }
  abc::TryRegion region;
  region.offset = snapshot_pc;
  if (shape == SnapshotShape::kDeadHandler) region.offset = {2};
  region.length = 1;
  region.handler_count = 1;
  body.tries.push_back(region);
  abc::CatchHandler handler;
  handler.offset = handler_pc;
  handler.length = 1;
  body.handlers.push_back(handler);
  const auto built = BuildExceptionFlow(body, profile, frame.registers, {}, {}, &frame);
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
  auto propagated = ExceptionValueFlow::Build(flow, ssa, entries, value_quota,
      {}, PendingExceptionValues::kLanguage);
  const auto& values = std::get<ExceptionValueFlow>(propagated);
  const ArgumentFrameFact owner{frame};
  const auto checked = VerifyExceptionInputs(flow, ssa, environments, values,
      kDefaultSemanticWork, &owner);
  const auto& proof = std::get<ExceptionInputProof>(checked);
  const PhasedValueInput input{flow, ssa, environments, values, proof, frame};
  const auto imported = ImportPhasedValues(input);
  test::Check(std::holds_alternative<ValueProgram>(imported));
  const auto& graph = std::get<ValueProgram>(imported);
  test::Check(graph.ops.size() == body.instructions.size());
  test::Check(graph.phases.size() == flow.nodes.size());
  test::Check(graph.arguments == owner);
  const auto& snapshot = graph.ops[snapshot_index];
  test::Check(std::holds_alternative<ValueArgumentSnapshot>(snapshot.operation));
  test::Check(snapshot.use_count == parameters);
  auto missing_environment = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  missing_environment.before[snapshot.block.value].reset();
  const PhasedValueInput missing{flow, ssa, missing_environment, values, proof, frame};
  test::Check(std::get<ValueImportFailure>(ImportPhasedValues(missing)).kind == ValueImportError::kInputMismatch);
  test::Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(graph, missing)));
  test::Check(std::holds_alternative<PhasedValueFlowProof>(VerifyPhasedValueFlow(graph)));
  const auto verified = VerifyImportedPhases(graph, input);
  test::Check(std::holds_alternative<PhasedImportProof>(verified));
  const auto work = std::get<PhasedImportProof>(verified).work;
  test::Check(std::holds_alternative<PhasedImportProof>(VerifyImportedPhases(
      graph, input, {SemanticBudget{}.bytes, work})));
  test::Check(std::get<ValueImportFailure>(VerifyImportedPhases(
      graph, input, {SemanticBudget{}.bytes, work - 1})).kind == ValueImportError::kWorkLimit);
  std::size_t catches = 0;
  for (const auto& value : graph.values) {
    if (const auto* result = std::get_if<OpResult>(&value.origin)) {
      test::Check(result->commit.has_value());
      test::Check(graph.phases[result->commit->value].phase == ExceptionPhase::kCommit);
    } else if (const auto* caught = std::get_if<CatchDefinition>(&value.origin)) {
      ++catches;
      test::Check(graph.phases[caught->block.value].phase == ExceptionPhase::kCatch);
    }
  }
  if (shape == SnapshotShape::kDeadHandler) {
    test::Check(catches == 0 && graph.phases.back().phase == ExceptionPhase::kCatch &&
        graph.phases.back().reachability == PhaseReachability::kUnreachable &&
        graph.blocks.back().edge_count == 0 && graph.ops[2].life == ValueLife::kRemoved);
    auto changed = graph; changed.phases.back().reachability = PhaseReachability::kReachable;
    test::Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(changed, input)));
    return;
  }
  test::Check(catches == 1);
  if (shape == SnapshotShape::kDeadLoop)
    test::Check(graph.ops[2].life == ValueLife::kRemoved && graph.blocks[graph.ops[2].block.value].edge_count == 0);
  if (shape == SnapshotShape::kDeadCode) {
    test::Check(graph.ops[2].life == ValueLife::kRemoved && graph.ops[2].use_count == 0 &&
        !graph.ops[2].result && graph.ops[3].life == ValueLife::kRemoved);
    auto changed = graph;
    changed.phases[graph.ops[2].block.value].reachability = PhaseReachability::kReachable;
    test::Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(changed, input)));
    changed = graph; changed.ops[2].normal = LanguageEffect::kHeapWrite;
    test::Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(changed, input)));
    changed = graph; changed.ops[0].life = ValueLife::kRemoved;
    test::Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(changed, input)));
    changed = graph;
    const auto value = *graph.ops[0].result;
    changed.values[value.value].origin = OpResult{{2}, WorkBlockId{graph.ops[2].block.value + 1}};
    changed.ops[2].result = value;
    test::Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(changed, input)));
    const auto built = std::get<ImportedPhases>(BuildPhasedValues(input));
    static std::uint64_t previous_work = 0;
    static std::size_t previous_bytes = 0;
    if (parameters >= 200) {
      if (previous_work) {
        test::Check(built.work < previous_work * 3);
        test::Check(built.allocated_bytes < previous_bytes * 3);
      }
      previous_work = built.work; previous_bytes = built.allocated_bytes;
    }
    // Upstream dominators may reserve a conservative admission ceiling larger
    // than the consumed charge. Find the exact composed admission boundary.
    std::size_t low = 0, bytes = SemanticBudget{}.bytes;
    while (low < bytes) {
      const auto mid = low + (bytes - low) / 2;
      const auto result = BuildPhasedValues(input, {mid, kDefaultSemanticWork});
      if (std::holds_alternative<ImportedPhases>(result)) bytes = mid;
      else { test::Check(std::get<ValueImportFailure>(result).kind == ValueImportError::kResourceLimit); low = mid + 1; }
    }
    test::Check(bytes >= built.allocated_bytes);
    test::Check(std::get<ValueImportFailure>(BuildPhasedValues(input, {bytes - 1, kDefaultSemanticWork})).kind ==
        ValueImportError::kResourceLimit);
  }
  if (shape == SnapshotShape::kHandlerJoin) {
    test::Check(!ssa.AllInputs().empty());
    const auto& returned = graph.ops.back();
    const auto result = std::get<ValueId>(graph.uses[returned.first_use.value].operand);
    test::Check(std::holds_alternative<PhiDefinition>(graph.values[result.value].origin));
  }
  if (shape == SnapshotShape::kLexicalPrelude) {
    test::Check(std::holds_alternative<ValueEnvironmentPush>(graph.ops.front().operation));
    test::Check(std::holds_alternative<ValueEnvironmentPop>(graph.ops[2].operation));
    test::Check(std::holds_alternative<EnvironmentId>(*environments.before[snapshot.block.value]));
    test::Check(graph.phases[snapshot.block.value].instruction.value == snapshot_index);
    for (std::size_t i = 0; i < snapshot.use_count; ++i)
      test::Check(graph.uses[snapshot.first_use.value + i].input->site.value == snapshot_index);
  }
  auto changed = graph;
  changed.phases.back().input = {0};
  test::Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(changed, input)));
  changed = graph;
  changed.origins[0].pc = {1};
  test::Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(changed, input)));
  changed = graph;
  changed.origins[0].original = abc::CodeOffset{1};
  test::Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(changed, input)));
  changed = graph;
  changed.ops[0].before_throw = LanguageEffect::kNone;
  test::Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(changed, input)));
  const auto exit = std::find_if(graph.exceptional_exits.begin(), graph.exceptional_exits.end(),
      [](const auto& value) { return value.handler.has_value(); });
  test::Check(exit != graph.exceptional_exits.end());
  const auto exit_index = static_cast<std::size_t>(exit - graph.exceptional_exits.begin());
  changed = graph;
  ++changed.exceptional_exits[exit_index].handler->dispatch.region.value;
  test::Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(changed, input)));
  changed = graph;
  changed.exceptional_exits[exit_index].handler->entry = {0};
  test::Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(changed, input)));
  changed = graph;
  changed.exceptional_exits[exit_index].handler.reset();
  test::Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(changed, input)));
  changed = graph;
  changed.values.back().domain = ValueKinds::Hole();
  test::Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(changed, input)));
  if (parameters != 0) {
    changed = graph;
    ++changed.uses[snapshot.first_use.value].input->ordinal;
    test::Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(changed, input)));
  }
  test::Check(std::get<ValueImportFailure>(ImportPhasedValues(input, {0})).kind ==
      ValueImportError::kResourceLimit);
  test::Check(std::get<ValueImportFailure>(ImportPhasedValues(input, {SemanticBudget{}.bytes, 0})).kind ==
      ValueImportError::kWorkLimit);
}
void ReadOldLocal() {
  // r0 = parameter; try { r0 = parameter.field; return r0; }
  // catch { return r0; } must read the pre-throw r0, not the normal store.
  auto body = test::Body({static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 1,
      static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0,
      static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kRETURN),
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
  abc::TryRegion region;
  region.offset = {4}; region.length = 4; region.handler_count = 1;
  body.tries.push_back(region);
  abc::CatchHandler handler;
  handler.offset = {11}; handler.length = 3;
  body.handlers.push_back(handler);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(1, 1, {}));
  auto built = BuildExceptionFlow(body, abc::ProfileId::kDynamic1301, frame.registers, {}, {}, &frame);
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
  const auto imported = ImportPhasedValues(input);
  test::Check(std::holds_alternative<ValueProgram>(imported));
  const auto& graph = std::get<ValueProgram>(imported);
  test::Check(!graph.arguments);
  test::Check(std::holds_alternative<ValueNamedRead>(graph.ops[2].operation));
  const auto old_local = graph.ops[1].result;
  const auto new_local = graph.ops[3].result;
  test::Check(old_local && new_local && old_local != new_local);
  const auto caught_load = std::get<ValueId>(graph.uses[graph.ops[5].first_use.value].operand);
  test::Check(caught_load == *old_local);
  test::Check(std::holds_alternative<PhasedImportProof>(VerifyImportedPhases(graph, input)));
  auto changed = graph;
  changed.exceptional_exits.clear();
  test::Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(changed, input)));
  auto wrong_frame = frame;
  ++wrong_frame.locals;
  const PhasedValueInput mismatch{flow, ssa, environments, values, proof, wrong_frame};
  test::Check(std::get<ValueImportFailure>(ImportPhasedValues(mismatch)).kind ==
      ValueImportError::kInputMismatch);
}
}  // namespace
int main() {
  for (const auto profile : {abcd::abc::ProfileId::kDynamic1202,
      abcd::abc::ProfileId::kDynamic1206, abcd::abc::ProfileId::kDynamic1301})
    for (const std::uint32_t count : {0U, 1U, 200U, 201U, 402U}) Snapshot(count, profile);
  Snapshot(1, abcd::abc::ProfileId::kDynamic1301, SnapshotShape::kHandlerJoin);
  Snapshot(1, abcd::abc::ProfileId::kDynamic1301, SnapshotShape::kLexicalPrelude);
  Snapshot(1, abcd::abc::ProfileId::kDynamic1301, SnapshotShape::kDeadHandler);
  Snapshot(1, abcd::abc::ProfileId::kDynamic1301, SnapshotShape::kDeadLoop);
  for (const auto count : {1U, 200U, 201U, 400U, 800U})
    Snapshot(count, abcd::abc::ProfileId::kDynamic1301, SnapshotShape::kDeadCode);
  ReadOldLocal();
}
