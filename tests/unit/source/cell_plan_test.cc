#include "../../../src/source/cell_plan.h"
#include "../../../src/source/cell_lifetime.h"
#include "../../../src/source/cell_initialization.h"
#include "../../../src/source/evaluation_plan.h"
#include "../../../src/source/expression_forest.h"
#include "abcd/analysis/value_simplify.h"
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
  if (count != 0 && count <= 2) {
    const auto evaluated = BuildEvaluationPlan(graph);
    const auto* evaluation = std::get_if<EvaluationPlan>(&evaluated);
    test::Check(evaluation && evaluation->cells);
    std::vector<ExpressionConstant> constants(graph.ops.size());
    const auto expressions = std::get<ExpressionForest>(BuildExpressionForest(
        graph, *evaluation, frame.parameters, {0}, {constants, 0}));
    test::Check(expressions.bindings.cells.size() == count && !expressions.operations[0]);
    for (std::size_t c = 0; c < plan->cells.size(); ++c) {
      const auto symbol = expressions.bindings.cells[c];
      test::Check(std::get<CellBinding>(expressions.bindings.symbols[symbol.value]).cell == CellId{static_cast<std::uint32_t>(c)});
      const auto& cell = plan->cells[c];
      for (std::uint32_t i = 0; i < cell.count; ++i) {
        const auto access = plan->accesses[cell.first + i];
        const auto expression = *expressions.operations[access.operation.value];
        const auto& node = expressions.expressions[expression.value];
        test::Check(node.origin.value == graph.ops[access.operation.value].origin.value);
        if (access.kind == CellAccessKind::kLoad)
          test::Check(std::get<SymbolExpr>(node.expression).symbol == symbol);
        else test::Check(std::get<SymbolTarget>(std::get<AssignmentExpr>(node.expression).target).symbol == symbol);
      }
    }
    test::Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(graph, *evaluation)));
    test::Check(evaluation->operations[0].kind == EvaluationKind::kEnvironment);
    auto forged = *evaluation; forged.cells.reset();
    test::Check(std::get<EvaluationError>(VerifyEvaluationPlan(graph, forged)) == EvaluationError::kOwnership);
    forged = *evaluation; forged.operations[0].kind = EvaluationKind::kRoot;
    test::Check(std::get<EvaluationError>(VerifyEvaluationPlan(graph, forged)) == EvaluationError::kOwnership);
    auto observed = graph;
    // A fabricated Language tag on the otherwise unused environment result
    // must not allow it to become a printable source value.
    observed.values[observed.ops[0].result->value].domain = ValueKinds::Language();
    test::Check(std::get<CellEvaluationError>(BuildCellEvaluation(observed)) == CellEvaluationError::kInternalIdentity);
  }
  test::Check(std::holds_alternative<std::uint64_t>(VerifyCellPlan(graph, environments, *plan)));
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto initialized = std::get<CellInitializations>(BuildCellInitializations(graph, environments, *plan));
  test::Check(std::holds_alternative<std::uint64_t>(VerifyCellInitializations(
      graph, environments, *plan, ClosureWrites::kUnknown, initialized)));
  SemanticBudget initialization_budget; initialization_budget.work = initialized.work;
  test::Check(std::holds_alternative<CellInitializations>(BuildCellInitializations(
      graph, environments, *plan, ClosureWrites::kUnknown, initialization_budget)));
  --initialization_budget.work;
  test::Check(std::get<CellInitializationError>(BuildCellInitializations(
      graph, environments, *plan, ClosureWrites::kUnknown, initialization_budget)) == CellInitializationError::kWorkLimit);
  for (const auto& cell : plan->cells) {
    const auto& last_store = plan->accesses[cell.first + 1];
    const auto& load = plan->accesses[cell.first + 2];
    test::Check(std::get<StoredInitialization>(initialized.reads[load.operation.value]).store == last_store.operation);
    auto forged = initialized;
    forged.reads[load.operation.value] = StoredInitialization{load.operation};
    test::Check(std::get<CellInitializationError>(VerifyCellInitializations(
        graph, environments, *plan, ClosureWrites::kUnknown, forged)) == CellInitializationError::kWitness);
    // Another dominating store is a valid initialization witness, even though
    // it is NOT the reaching value. The independent verifier must allow this.
    forged.reads[load.operation.value] = StoredInitialization{plan->accesses[cell.first].operation};
    test::Check(std::holds_alternative<std::uint64_t>(VerifyCellInitializations(
        graph, environments, *plan, ClosureWrites::kUnknown, forged)));
    break;
  }
  const auto lifetime = std::get<CellLifetimes>(BuildCellLifetimes(graph, environments, *plan, control));
  test::Check(lifetime.scopes.size() == 1 && std::holds_alternative<CellBodyScope>(lifetime.scopes[0].origin));
  for (const auto& cell : lifetime.cells) test::Check(std::get<CellScopeId>(cell) == CellScopeId{0});
  SemanticBudget lifetime_budget; lifetime_budget.work = lifetime.work;
  test::Check(std::holds_alternative<CellLifetimes>(BuildCellLifetimes(graph, environments, *plan, control, lifetime_budget)));
  --lifetime_budget.work;
  test::Check(std::get<CellLifetimeError>(BuildCellLifetimes(graph, environments, *plan, control, lifetime_budget)) == CellLifetimeError::kWorkLimit);
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
    test::Check(FindCell(*plan, plan->cells.front().storage) == CellId{0});
    test::Check(!FindCell(*plan, IncomingCell{{0}, {0}}));
    forged = *plan; ++forged.allocations[0].value;
    test::Check(std::get<CellError>(VerifyCellPlan(graph, environments, forged)) == CellError::kStorage);
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
  const auto initialized = std::get<CellInitializations>(BuildCellInitializations(graph, environments, plan));
  test::Check(std::holds_alternative<IncomingInitialization>(initialized.reads[0]));
  test::Check(plan.cells.size() == 1 && std::get<IncomingCell>(plan.cells[0].storage).depth.value == 0);
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto lifetime = std::get<CellLifetimes>(BuildCellLifetimes(graph, environments, plan, control));
  test::Check(std::holds_alternative<IncomingLifetime>(lifetime.cells[0]));
}
void Iteration() {
  // Allocation and accesses repeat inside the loop; pop restores the entry
  // environment before the backedge. A function-body declaration is invalid.
  constexpr std::uint8_t kPush = 0x09, kLoad = 0x60, kStore = 0x3d,
      kRead = 0x3c, kPop = 0x69, kJumpNonzero = 0x51, kReturnUndefined = 0x65;
  constexpr std::int8_t kBackedgeDelta = -11;
  const auto body = test::Body({kPush, 1, kLoad, 0, kStore, 0, kRead, 0,
      kPop, kLoad, 0, kJumpNonzero, static_cast<std::uint8_t>(kBackedgeDelta), kReturnUndefined});
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {false, false, false, false}));
  const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, frame.registers));
  const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, input, frame.registers, frame.locals));
  const auto graph = std::get<ValueProgram>(ImportNormalValues(body, input, proof, frame));
  const auto environments = std::get<ValueEnvironments>(BuildValueEnvironments(graph));
  const auto cells = std::get<CellPlan>(BuildCellPlan(graph, environments));
  const auto initialized = std::get<CellInitializations>(BuildCellInitializations(graph, environments, cells));
  test::Check(std::holds_alternative<std::uint64_t>(VerifyCellInitializations(
      graph, environments, cells, ClosureWrites::kUnknown, initialized)));
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto lifetime = std::get<CellLifetimes>(BuildCellLifetimes(graph, environments, cells, control));
  const auto scope = std::get<CellScopeId>(lifetime.cells[0]);
  test::Check(scope != CellScopeId{0} && std::holds_alternative<CellLoopScope>(lifetime.scopes[scope.value].origin));
}
void ClosurePremise() {
  constexpr std::uint8_t kPush = 0x09, kLoad = 0x60, kStore = 0x3d,
      kRead = 0x3c, kPop = 0x69, kFunction = 0x33, kReturn = 0x64;
  const auto body = test::Body({kPush, 1, kLoad, 0, kStore, 0,
      kFunction, 0, 0, 0, 0, kRead, 0, kPop, kReturn});
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {false, false, false, false}));
  const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, frame.registers));
  // Conditional input proof is not a validated child or publication permission.
  const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, input, frame.registers,
      frame.locals, {}, CapturedValues::kReject, {}, ClosureWrites::kLanguage));
  const auto graph = std::get<ValueProgram>(ImportNormalValues(body, input, proof, frame));
  const auto environments = std::get<ValueEnvironments>(BuildValueEnvironments(graph));
  const auto cells = std::get<CellPlan>(BuildCellPlan(graph, environments));
  test::Check(std::get<CellInitializationError>(BuildCellInitializations(graph, environments, cells)) == CellInitializationError::kClosure);
  const auto initialized = std::get<CellInitializations>(BuildCellInitializations(graph, environments, cells, ClosureWrites::kLanguage));
  test::Check(std::get<CellInitializationError>(VerifyCellInitializations(
      graph, environments, cells, ClosureWrites::kUnknown, initialized)) == CellInitializationError::kWitness);
}
void ReadSnapshot() {
  // Read a cell, retain its value, mutate the cell, return the old value.
  // A scalar SSA snapshot must never be rebound to the mutable cell symbol.
  constexpr std::uint8_t kPush = 0x09, kLoad = 0x60, kSave = 0x61,
      kStoreCell = 0x3d, kReadCell = 0x3c, kInteger = 0x62, kPop = 0x69, kReturn = 0x64;
  const auto body = test::Body({kPush, 1, kLoad, 0, kStoreCell, 0, kReadCell, 0,
      kSave, 1, kInteger, 2, 0, 0, 0, kStoreCell, 0, kPop, kLoad, 1, kReturn});
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 2, {false, false, false, false}));
  const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, frame.registers));
  const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, input, frame.registers, frame.locals));
  auto graph = std::get<ValueProgram>(ImportNormalValues(body, input, proof, frame));
  test::Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph, graph.revision)));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  std::vector<ExpressionConstant> constants(graph.ops.size());
  const auto expressions = std::get<ExpressionForest>(BuildExpressionForest(
      graph, evaluation, frame.parameters, {0}, {constants, 0}));
  constexpr std::size_t kCellReadOperation = 3;
  const auto& load = graph.ops[kCellReadOperation];
  test::Check(evaluation.operations[kCellReadOperation].kind == EvaluationKind::kRoot &&
      evaluation.operations[kCellReadOperation].temporary.has_value());
  const auto snapshot = std::get<SymbolId>(expressions.bindings.values[load.result->value]);
  const auto cell = expressions.bindings.cells[0];
  test::Check(snapshot != cell && std::holds_alternative<SnapshotBinding>(expressions.bindings.symbols[snapshot.value]));
  const auto syntax = *expressions.operations[kCellReadOperation];
  test::Check(std::get<SymbolExpr>(expressions.expressions[syntax.value].expression).symbol == cell);
}
}  // namespace
int main() { for (const auto count : {0U, 1U, 2U, 200U, 201U}) Run(count); Incoming(); Iteration(); ClosurePremise(); ReadSnapshot(); }
