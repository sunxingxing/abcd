#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/cell_plan.h"
#include "../../../src/source/cell_initialization.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
void Run(abc::ProfileId profile, std::size_t count = 1) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  // Failed allocation reaches a store in the incoming environment; the normal
  // store belongs to the new allocation despite sharing lexical depth/slot.
  const std::vector<std::uint8_t> sequence{op(abc::Opcode::kNEWLEXENV_IMM8), 1,
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
      op(abc::Opcode::kPOPLEXENV)};
  std::vector<std::uint8_t> bytes;
  for (std::size_t i = 0; i < count; ++i) bytes.insert(bytes.end(), sequence.begin(), sequence.end());
  bytes.push_back(op(abc::Opcode::kRETURNUNDEFINED));
  const auto catch_pc = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0, op(abc::Opcode::kRETURN)});
  auto body = test::Body(bytes);
  abc::TryRegion region; region.length = 2; region.handler_count = 1;
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
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
      value_work, {}, PendingExceptionValues::kLanguage));
  const auto proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, env, values));
  const auto graph = std::get<ValueProgram>(ImportPhasedValues({flow, ssa, env, values, proof, frame}));
  const auto environments = std::get<ValueEnvironments>(BuildPhasedValueEnvironments(graph));
  auto built = BuildPhasedCellPlan(graph, environments);
  test::Check(std::holds_alternative<CellPlan>(built));
  auto& cells = std::get<CellPlan>(built);
  const auto catch_store = cells.operations.size() - 2;
  test::Check(cells.cells.size() == count + 1 && cells.accesses.size() == count + 1);
  test::Check(cells.operations[2] != cells.operations[catch_store]);
  const auto allocated = FindCell(cells, AllocatedCell{{0}, {0}});
  const auto incoming = FindCell(cells, IncomingCell{{0}, {0}});
  test::Check(allocated == cells.operations[2] && incoming == cells.operations[catch_store]);
  const auto verify = [&] { return VerifyPhasedCellPlan(graph, environments, cells); };
  const auto checked = verify();
  test::Check(std::holds_alternative<std::uint64_t>(checked));
  test::Check(std::get<CellError>(BuildCellPlan(graph, environments)) == CellError::kEnvironment);
  test::Check(std::get<CellError>(VerifyCellPlan(graph, environments, cells)) == CellError::kEnvironment);
  test::Check(std::get<CellInitializationError>(BuildCellInitializations(
      graph, environments, cells, ClosureWrites::kUnknown)) == CellInitializationError::kCells);
  SemanticBudget budget;
  budget.work = cells.work;
  test::Check(std::holds_alternative<CellPlan>(BuildPhasedCellPlan(graph, environments, budget)));
  --budget.work;
  test::Check(std::get<CellError>(BuildPhasedCellPlan(graph, environments, budget)) == CellError::kWorkLimit);
  budget = {}; budget.bytes = 0;
  test::Check(std::get<CellError>(BuildPhasedCellPlan(graph, environments, budget)) == CellError::kResourceLimit);
  budget = {}; budget.work = std::get<std::uint64_t>(checked);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedCellPlan(graph, environments, cells, budget)));
  --budget.work;
  test::Check(std::get<CellError>(VerifyPhasedCellPlan(graph, environments, cells, budget)) == CellError::kWorkLimit);
  ++cells.revision.value;
  test::Check(std::get<CellError>(verify()) == CellError::kRevision);
  --cells.revision.value;
  cells.operations[catch_store] = allocated;
  test::Check(std::get<CellError>(verify()) == CellError::kAccess);
  cells.operations[catch_store] = incoming;
  cells.allocations[0] = {1};
  test::Check(std::get<CellError>(verify()) == CellError::kStorage);
  cells.allocations[0] = {0};
  cells.accesses[0].kind = CellAccessKind::kLoad;
  test::Check(std::get<CellError>(verify()) == CellError::kAccess);
  cells.accesses[0].kind = CellAccessKind::kStore;
  test::Check(std::holds_alternative<std::uint64_t>(verify()));
}
}  // namespace
int main() {
  for (const auto profile : {abcd::abc::ProfileId::kDynamic1202, abcd::abc::ProfileId::kDynamic1206,
                            abcd::abc::ProfileId::kDynamic1301}) Run(profile);
  for (const std::size_t count : {200, 201, 402}) Run(abcd::abc::ProfileId::kDynamic1301, count);
}
