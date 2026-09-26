#include "../../../src/source/cell_plan.h"
#include "abcd/analysis/value_import.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
void Run(std::uint32_t count) {
  // Pinned v7: each push creates fresh slot zero; two writes intentionally share
  // that cell. Equal slots across allocations must not be merged.
  constexpr std::uint8_t kPush = 0x09, kLoad = 0x60, kStore = 0x3d,
      kRead = 0x3c, kPop = 0x69, kReturnUndefined = 0x65;
  std::vector<std::uint8_t> code;
  for (std::uint32_t i = 0; i < count; ++i)
    code.insert(code.end(), {kPush, 1, kLoad, 0, kStore, 0, kStore, 0, kRead, 0, kPop});
  code.push_back(kReturnUndefined);
  const auto body = test::Body(code);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {false, false, false, false}));
  const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, frame.registers));
  const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, input, frame.registers, frame.locals));
  const auto graph = std::get<ValueProgram>(ImportNormalValues(body, input, proof, frame));
  const auto environments = std::get<ValueEnvironments>(BuildValueEnvironments(graph));
  const auto planned = BuildCellPlan(graph, environments);
  const auto* plan = std::get_if<CellPlan>(&planned);
  test::Check(plan && plan->cells.size() == count && plan->operations.size() == graph.ops.size());
  test::Check(std::holds_alternative<std::uint64_t>(VerifyCellPlan(graph, environments, *plan)));
  for (std::size_t i = 0; i < plan->cells.size(); ++i) {
    const auto& cell = plan->cells[i];
    test::Check(cell.count == 3 && std::get<AllocatedCell>(cell.storage).slot.value == 0);
    test::Check(plan->accesses[cell.first].kind == CellAccessKind::kStore &&
        plan->accesses[cell.first + 1].kind == CellAccessKind::kStore &&
        plan->accesses[cell.first + 2].kind == CellAccessKind::kLoad);
    if (i) test::Check(std::get<AllocatedCell>(cell.storage).allocation !=
        std::get<AllocatedCell>(plan->cells[i - 1].storage).allocation);
  }
  auto forged = *plan; ++forged.revision.value;
  test::Check(std::get<CellError>(VerifyCellPlan(graph, environments, forged)) == CellError::kRevision);
  if (count) {
    forged = *plan; forged.accesses[0].kind = CellAccessKind::kLoad;
    test::Check(std::get<CellError>(VerifyCellPlan(graph, environments, forged)) == CellError::kAccess);
    forged = *plan; ++std::get<AllocatedCell>(forged.cells[0].storage).slot.value;
    test::Check(std::holds_alternative<CellError>(VerifyCellPlan(graph, environments, forged)));
    forged = *plan; forged.operations[0] = CellId{0};
    test::Check(std::get<CellError>(VerifyCellPlan(graph, environments, forged)) == CellError::kCoverage);
    forged = *plan; forged.accesses[1] = forged.accesses[0];
    test::Check(std::get<CellError>(VerifyCellPlan(graph, environments, forged)) == CellError::kAccess);
  }
  SemanticBudget budget; budget.work = plan->work;
  test::Check(std::holds_alternative<CellPlan>(BuildCellPlan(graph, environments, budget)));
  --budget.work;
  test::Check(std::get<CellError>(BuildCellPlan(graph, environments, budget)) == CellError::kWorkLimit);
  budget = {}; budget.bytes = 0;
  test::Check(std::get<CellError>(BuildCellPlan(graph, environments, budget)) == CellError::kResourceLimit);
}
void Incoming() {
  constexpr std::uint8_t kLoadCell = 0x3c, kReturn = 0x64;
  const auto body = test::Body({kLoadCell, 0, kReturn});
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {false, false, false, false}));
  const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, frame.registers));
  const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, input, frame.registers, frame.locals, {}, CapturedValues::kLanguage));
  const auto graph = std::get<ValueProgram>(ImportNormalValues(body, input, proof, frame));
  const auto environments = std::get<ValueEnvironments>(BuildValueEnvironments(graph));
  const auto plan = std::get<CellPlan>(BuildCellPlan(graph, environments));
  test::Check(plan.cells.size() == 1 && std::get<IncomingCell>(plan.cells[0].storage).depth.value == 0);
}
}  // namespace
int main() { for (const auto count : {0U, 1U, 2U, 200U, 201U}) Run(count); Incoming(); }
