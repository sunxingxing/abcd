#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/cell_initialization.h"
#include "../../../src/source/evaluation_plan.h"
#include "../../../src/source/exception_tree.h"
#include "abcd/analysis/exception_locals.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class StoreOrder { kBeforeCall, kAfterCall };
enum class Exit { kReturn, kThrow };
void Run(abc::ProfileId profile, StoreOrder order, std::size_t count = 1,
    Exit exit = Exit::kReturn, bool buffered_capture = false) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr std::uint8_t kArgument = 3;
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kNEWLEXENV_IMM8), 1,
      op(abc::Opcode::kLDA_V8), kArgument};
  if (order == StoreOrder::kBeforeCall)
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0});
  if (buffered_capture)
    bytes.insert(bytes.end(), {op(abc::Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16), 0, 0, 0,
        op(abc::Opcode::kLDA_V8), kArgument});
  const auto call_pc = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {op(abc::Opcode::kCALLARG0_IMM8), 0});
  if (order == StoreOrder::kAfterCall)
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kArgument,
        op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0});
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0,
      op(abc::Opcode::kPOPLEXENV)});
  for (std::size_t i = 1; i < count; ++i)
    bytes.insert(bytes.end(), {op(abc::Opcode::kNEWLEXENV_IMM8), 1,
        op(abc::Opcode::kLDA_V8), kArgument, op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
        op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, op(abc::Opcode::kPOPLEXENV)});
  if (exit == Exit::kReturn) bytes.push_back(op(abc::Opcode::kRETURN));
  else {
    constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
    bytes.push_back(static_cast<std::uint8_t>(kThrow));
    bytes.push_back(static_cast<std::uint8_t>(kThrow >> 8));
  }
  const auto catch_pc = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, op(abc::Opcode::kRETURN)});
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {call_pc}; region.length = 2; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {catch_pc}; handler.length = 3;
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
  auto local = ProveExceptionLocals(flow, env);
  std::vector<ExceptionLoadFact> loads;
  if (order == StoreOrder::kBeforeCall && !buffered_capture) loads = std::get<ExceptionLocals>(local).loads;
  else {
    test::Check(std::get<ExceptionLocalFailure>(local).kind == (buffered_capture
        ? ExceptionLocalError::kCapturedEnvironment : ExceptionLocalError::kUninitialized));
    // Adversarial conditional domain premise. The working initialization
    // verifier must independently reject the missing catch-path store.
    for (std::size_t b = 0; b < flow.nodes.size(); ++b) {
      const auto& node = flow.nodes[b];
      if (node.phase == ExceptionPhase::kCommit &&
          std::holds_alternative<LexicalLoad>(flow.instructions[node.instruction.value].operation))
        loads.push_back({{static_cast<std::uint32_t>(b)}});
    }
  }
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
      value_work, {}, PendingExceptionValues::kLanguage, loads));
  const auto proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, env, values));
  const auto graph = std::get<ValueProgram>(ImportPhasedValues({flow, ssa, env, values, proof, frame}));
  const auto environments = std::get<ValueEnvironments>(BuildPhasedValueEnvironments(graph));
  const auto cells = std::get<CellPlan>(BuildPhasedCellPlan(graph, environments));
  auto built = BuildPhasedCellInitializations(graph, environments, cells);
  if (buffered_capture) {
    test::Check(std::get<CellInitializationError>(built) == CellInitializationError::kClosure);
    return;
  }
  if (order == StoreOrder::kAfterCall) {
    test::Check(std::get<CellInitializationError>(built) == CellInitializationError::kUninitialized);
    CellInitializations forged{graph.revision, graph.cfg_revision, ClosureWrites::kUnknown};
    forged.reads.resize(graph.ops.size());
    constexpr OpId kLateStore{4};
    for (const auto& access : cells.accesses)
      if (access.kind == CellAccessKind::kLoad) forged.reads[access.operation.value] = StoredInitialization{kLateStore};
    test::Check(std::get<CellInitializationError>(VerifyPhasedCellInitializations(
        graph, environments, cells, ClosureWrites::kUnknown, forged)) == CellInitializationError::kUninitialized);
    test::Check(std::get<CellEvaluationError>(BuildPhasedCellEvaluation(graph)) == CellEvaluationError::kInitialization);
    test::Check(std::get<EvaluationError>(BuildPhasedEvaluationPlan(graph)) == EvaluationError::kUnsupportedOperation);
    return;
  }
  test::Check(std::holds_alternative<CellInitializations>(built));
  auto& plan = std::get<CellInitializations>(built);
  const auto verify = [&] { return VerifyPhasedCellInitializations(
      graph, environments, cells, ClosureWrites::kUnknown, plan); };
  const auto checked = verify();
  test::Check(std::holds_alternative<std::uint64_t>(checked));
  for (const auto& access : cells.accesses)
    if (access.kind == CellAccessKind::kLoad) {
      const auto cell = *cells.operations[access.operation.value];
      const auto allocation = std::get<AllocatedCell>(cells.cells[cell.value].storage).allocation;
      constexpr std::uint32_t kStoreOffset = 2;
      test::Check(std::get<StoredInitialization>(plan.reads[access.operation.value]).store ==
          OpId{allocation.value + kStoreOffset});
    }
  test::Check(std::get<CellInitializationError>(VerifyCellInitializations(
      graph, environments, cells, ClosureWrites::kUnknown, plan)) == CellInitializationError::kCells);
  SemanticBudget budget;
  budget.work = plan.work;
  test::Check(std::holds_alternative<CellInitializations>(BuildPhasedCellInitializations(
      graph, environments, cells, ClosureWrites::kUnknown, budget)));
  --budget.work;
  test::Check(std::get<CellInitializationError>(BuildPhasedCellInitializations(
      graph, environments, cells, ClosureWrites::kUnknown, budget)) == CellInitializationError::kWorkLimit);
  budget = {}; budget.bytes = 0;
  test::Check(std::get<CellInitializationError>(BuildPhasedCellInitializations(
      graph, environments, cells, ClosureWrites::kUnknown, budget)) == CellInitializationError::kResourceLimit);
  budget = {}; budget.work = std::get<std::uint64_t>(checked);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedCellInitializations(
      graph, environments, cells, ClosureWrites::kUnknown, plan, budget)));
  --budget.work;
  test::Check(std::get<CellInitializationError>(VerifyPhasedCellInitializations(
      graph, environments, cells, ClosureWrites::kUnknown, plan, budget)) == CellInitializationError::kWorkLimit);
  const auto catch_load = graph.ops.size() - 2;
  const auto witness = plan.reads[catch_load];
  plan.reads[catch_load] = IncomingInitialization{};
  test::Check(std::get<CellInitializationError>(verify()) == CellInitializationError::kWitness);
  plan.reads[catch_load] = StoredInitialization{{0}};
  test::Check(std::get<CellInitializationError>(verify()) == CellInitializationError::kWitness);
  plan.reads[catch_load] = witness;
  ++plan.revision.value;
  test::Check(std::get<CellInitializationError>(verify()) == CellInitializationError::kRevision);
  --plan.revision.value;
  test::Check(std::holds_alternative<std::uint64_t>(verify()));
  if (count == 1) {
    const auto evaluated = BuildPhasedCellEvaluation(graph);
    test::Check(std::holds_alternative<CellEvaluation>(evaluated));
    test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedCellEvaluation(
        graph, std::get<CellEvaluation>(evaluated))));
    test::Check(std::get<CellEvaluationError>(VerifyCellEvaluation(
        graph, std::get<CellEvaluation>(evaluated))) == CellEvaluationError::kEnvironment);
    const auto evaluation = BuildPhasedEvaluationPlan(graph);
    test::Check(std::holds_alternative<EvaluationPlan>(evaluation));
    test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedEvaluationPlan(
        graph, std::get<EvaluationPlan>(evaluation))));
    const PhasedValueInput input{flow, ssa, env, values, proof, frame};
    const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
    const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
    // Initialization alone must not open source emission across exception
    // scopes. The later lifetime bridge must replace this refusal explicitly.
    const auto tree = BuildExceptionFunctionDraft(graph, {body, input, regions, control},
        std::get<EvaluationPlan>(evaluation), {}, {});
    test::Check(std::get<FunctionTreeError>(tree) == FunctionTreeError::kBinding);
  }
}
}  // namespace
int main() {
  for (const auto profile : {abcd::abc::ProfileId::kDynamic1202, abcd::abc::ProfileId::kDynamic1206,
                            abcd::abc::ProfileId::kDynamic1301}) {
    Run(profile, StoreOrder::kBeforeCall);
    Run(profile, StoreOrder::kAfterCall);
    Run(profile, StoreOrder::kBeforeCall, 1, Exit::kThrow);
    Run(profile, StoreOrder::kBeforeCall, 1, Exit::kReturn, true);
  }
  for (const std::size_t count : {200, 201, 402})
    Run(abcd::abc::ProfileId::kDynamic1301, StoreOrder::kBeforeCall, count);
}
