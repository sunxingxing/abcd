#include "../../../src/source/capture_plan.h"
#include "abcd/analysis/exception_locals.h"
#include "analysis/ssa_fixture.h"
#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/cell_lifetime.h"
#include "../../../src/source/cell_evaluation.h"
#include "../../../src/source/exception_tree.h"

#include <iostream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
struct Input {
  abc::MethodBody body;
  ExceptionFlow flow;
  EnvironmentFlow environments;
  InstructionId creation;
  ExceptionNodeId before;
};
Input Make(unsigned width, bool copies, bool reset = false, bool extra = false, bool repeat = false) {
  std::vector<std::uint8_t> code;
  const auto op = [&](abc::Opcode x) { const auto n = static_cast<unsigned>(x);
    code.push_back(n & 255); if (n > 255) code.push_back(n >> 8); };
  op(abc::Opcode::kNEWLEXENV_IMM8); code.push_back(1);
  // Split normal paths before the capture without relying on physical layout.
  for (unsigned i = 0; i < width; ++i) {
    op(abc::Opcode::kJMP_IMM8); code.push_back(2);
    if (copies) { op(abc::Opcode::kLDA_V8); code.push_back(0); op(abc::Opcode::kSTA_V8); code.push_back(1); }
  }
  if (extra) { op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8); code.insert(code.end(), {0, 0, 0, 0}); }
  const auto header = code.size();
  if (repeat) { op(abc::Opcode::kLDA_V8); code.push_back(0); op(abc::Opcode::kJEQZ_IMM8); code.push_back(0); }
  const auto start = static_cast<std::uint32_t>(code.size());
  op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8); code.insert(code.end(), {0, 0, 0, 0});
  if (reset) { op(abc::Opcode::kLDUNDEFINED); op(abc::Opcode::kSTLEXVAR_IMM4_IMM4); code.push_back(0); }
  op(abc::Opcode::kCALLARG0_IMM8); code.push_back(0);
  op(abc::Opcode::kLDUNDEFINED); op(abc::Opcode::kSTLEXVAR_IMM4_IMM4); code.push_back(0);
  const auto end = static_cast<std::uint32_t>(code.size());
  if (repeat) {
    const auto back = code.size(); op(abc::Opcode::kJMP_IMM8);
    code.push_back(static_cast<std::uint8_t>(static_cast<int>(header) - static_cast<int>(back)));
    code[header + 3] = static_cast<std::uint8_t>(code.size() - (header + 2));
  }
  op(abc::Opcode::kRETURNUNDEFINED);
  const auto handler = static_cast<std::uint32_t>(code.size());
  op(abc::Opcode::kRETURNUNDEFINED);
  auto body = test::Body(code);
  body.tries.push_back({{start}, end - start, 0, 1});
  body.handlers.push_back({{}, {handler}, 1});
  auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, abc::ProfileId::kDynamic1301, 3));
  auto env = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  for (std::size_t i = 0; i < flow.nodes.size(); ++i) {
    const auto& node = flow.nodes[i];
    if (node.phase == ExceptionPhase::kBefore && flow.instructions[node.instruction.value].pc.value == start)
      return {std::move(body), std::move(flow), std::move(env), node.instruction, {static_cast<std::uint32_t>(i)}};
  }
  throw std::runtime_error("missing creation");
}
void Run(unsigned width, bool copies) {
  auto input = Make(width, copies);
  auto& flow = input.flow; auto& env = input.environments;
  const auto build = [&](SemanticBudget budget = {}) {
    return ProveExceptionLocals(flow, env, budget, {}, ClosureWrites::kLanguage);
  };
  auto locals = std::get<ExceptionLocals>(build());
  test::Check(locals.deferred_initializations.size() == 1);
  const auto verified = VerifyExceptionDeferredInitializations(flow, env, locals, {}, {}, ClosureWrites::kLanguage);
  test::Check(std::holds_alternative<std::uint64_t>(verified));
  const auto& operation = std::get<FunctionCreation>(flow.instructions[input.creation.value].operation);
  const CreationBinding creation{flow.instructions[input.creation.value].pc, operation, {{0}, {}},
      OrdinaryCapture{*env.before[input.before.value]}};
  std::vector<CaptureUse> uses(width, CaptureUse{{{0}, {0}}, CaptureDomain::kSourceAccess});
  const ScopeFacts scope(ExceptionScopeFacts{flow, env, locals, input.before, true});
  auto plan = std::get<CapturePlan>(BuildCapturePlan(scope, input.creation, creation, uses));
  test::Check(plan.bindings.front().deferred == locals.deferred_initializations.front().store);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyCapturePlan(scope, input.creation, creation, uses, plan)));
  const auto exact = SemanticBudget{plan.allocated_bytes, plan.work};
  test::Check(std::holds_alternative<CapturePlan>(BuildCapturePlan(scope, input.creation, creation, uses, exact)));
  test::Check(std::get<CaptureFailure>(BuildCapturePlan(scope, input.creation, creation, uses,
      {exact.bytes - 1, exact.work})) == CaptureError::kResourceLimit);
  test::Check(std::get<CaptureFailure>(BuildCapturePlan(scope, input.creation, creation, uses,
      {exact.bytes, exact.work - 1})) == CaptureError::kWorkLimit);
  const ScopeFacts previous(ExceptionScopeFacts{flow, env, locals, input.before, false});
  test::Check(std::get<CaptureFailure>(BuildCapturePlan(previous, input.creation, creation, uses)) == CaptureError::kScope);
  for (auto domain : {CaptureDomain::kLanguage, CaptureDomain::kDefined}) {
    auto wrong = uses; for (auto& use : wrong) use.domain = domain;
    test::Check(std::get<CaptureFailure>(BuildCapturePlan(scope, input.creation, creation, wrong)) == CaptureError::kScope);
  }
  for (auto mutate : {0, 1, 2, 3, 4, 5}) {
    auto bad = locals;
    auto& fact = bad.deferred_initializations.front();
    switch (mutate) {
      case 0: ++fact.cell.value; break;
      case 1: fact.store = input.creation; break;
      case 2: ++fact.begin; break;
      case 3: bad.plan.cells.front().slot.value++; break;
      case 4: bad.plan.cells.front().environment.value++; break;
      case 5: bad.plan.allocations.front() = input.creation; break;
    }
    test::Check(std::get<ExceptionLocalFailure>(VerifyExceptionDeferredInitializations(
        flow, env, bad, {}, {}, ClosureWrites::kLanguage)).kind == ExceptionLocalError::kInvalidEnvironment);
  }
  auto forged = plan; forged.bindings.front().deferred = input.creation;
  test::Check(std::get<CaptureFailure>(VerifyCapturePlan(scope, input.creation, creation, uses, forged)) == CaptureError::kBinding);
  auto unknown = std::get<ExceptionLocals>(ProveExceptionLocals(flow, env));
  test::Check(unknown.deferred_initializations.empty());
  std::size_t lo = 0, hi = SemanticBudget{}.bytes;
  while (lo < hi) { const auto mid = lo + (hi - lo) / 2;
    if (std::holds_alternative<ExceptionLocals>(build({mid}))) hi = mid; else lo = mid + 1; }
  auto bounded = std::get<ExceptionLocals>(build({lo}));
  test::Check(lo && std::get<ExceptionLocalFailure>(build({lo - 1})).kind == ExceptionLocalError::kResourceLimit);
  test::Check(std::holds_alternative<ExceptionLocals>(build({lo, bounded.plan.work_units})));
  test::Check(std::get<ExceptionLocalFailure>(build({lo, bounded.plan.work_units - 1})).kind == ExceptionLocalError::kResourceLimit);
  const auto vwork = std::get<std::uint64_t>(verified);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyExceptionDeferredInitializations(flow, env, locals,
      {SemanticBudget{}.bytes, vwork}, {}, ClosureWrites::kLanguage)));
  test::Check(std::get<ExceptionLocalFailure>(VerifyExceptionDeferredInitializations(flow, env, locals,
      {SemanticBudget{}.bytes, vwork - 1}, {}, ClosureWrites::kLanguage)).kind == ExceptionLocalError::kResourceLimit);
  std::cout << "width=" << width << " copies=" << copies << " locals=" << lo << '/'
      << bounded.plan.work_units << " plan=" << exact.bytes << '/' << exact.work << '\n';
}
void Native(unsigned width, bool copies, bool repeat = false) {
  auto raw = Make(width, copies, false, true, repeat);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(3, 0, {}));
  IdfQuota quota{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(raw.flow, frame.registers, quota));
  const auto locals = std::get<ExceptionLocals>(ProveExceptionLocals(raw.flow, raw.environments, {}, {}, ClosureWrites::kLanguage));
  std::vector<EntryValueFact> entries;
  for (std::uint32_t i = 0; i < ssa.Values().size(); ++i)
    if (std::holds_alternative<MethodEntry>(ssa.Values()[i].origin) &&
        std::holds_alternative<abc::RegisterId>(ssa.Values()[i].slot)) entries.push_back({{i}, ValueKinds::Language()});
  auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(raw.flow, ssa, entries, quota,
      {}, PendingExceptionValues::kLanguage, locals.loads, {}, CallResultValues::kLanguage));
  const auto proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(raw.flow, ssa, raw.environments, values));
  const PhasedValueInput input{raw.flow, ssa, raw.environments, values, proof, frame};
  const auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(raw.body, graph, input));
  const auto control = std::get<ExceptionControl>(BuildExceptionControl(raw.body, graph, input, regions));
  const ExceptionTreeInput exceptions{raw.body, input, regions, control};
  OpId creation{}, outside{};
  for (std::uint32_t i = 0; i < graph.ops.size(); ++i) {
    const auto& op = graph.ops[i];
    if (!std::holds_alternative<ValueFunctionCreation>(op.operation)) continue;
    if (graph.origins[op.origin.value].site.value == raw.creation.value) creation = {i};
    else outside = {i};
  }
  const CellDemand demand{creation, {{0}, {0}}, CaptureDomain::kSourceAccess};
  auto evaluation = std::get<CellEvaluation>(BuildPhasedCellEvaluation(graph, {}, IncomingCells::kReject,
      ClosureWrites::kLanguage, {&demand, 1}));
  OpId store{};
  for (std::uint32_t i = 0; i < graph.ops.size(); ++i)
    if (std::holds_alternative<ValueLexicalStore>(graph.ops[i].operation)) store = {i};
  const DeferredCellInitialization deferred{{0}, store};
  test::Check(MarkDeferredCells(graph, evaluation, {&deferred, 1}, {}).has_value());
  test::Check(VerifyDeferredCells(graph, evaluation, {}).has_value());
  const auto build = [&](const CellPlan& cells, SemanticBudget budget = {}) {
    return BuildPhasedCellLifetimes(graph, *evaluation.environments, cells, exceptions, budget,
        nullptr, nullptr, evaluation.deferred);
  };
  if (repeat) {
    test::Check(std::get<CellLifetimeError>(build(evaluation.cells)) == CellLifetimeError::kScopeEscape);
    return;
  }
  const auto mark = [&](SemanticBudget budget) {
    auto trial = evaluation;
    std::vector<std::optional<OpId>>{}.swap(trial.deferred);
    return MarkDeferredCells(graph, trial, {&deferred, 1}, budget);
  };
  std::size_t mark_lo = 0, mark_hi = SemanticBudget{}.bytes;
  while (mark_lo < mark_hi) { const auto mid = mark_lo + (mark_hi - mark_lo) / 2;
    if (mark({mid})) mark_hi = mid; else mark_lo = mid + 1; }
  const auto mark_work = *mark({mark_lo});
  test::Check(mark({mark_lo, mark_work}).has_value());
  test::Check(!mark({mark_lo - 1, mark_work}));
  test::Check(!mark({mark_lo, mark_work - 1}));
  const auto verify_work = *VerifyDeferredCells(graph, evaluation, {});
  test::Check(VerifyDeferredCells(graph, evaluation, {SemanticBudget{}.bytes, verify_work}).has_value());
  test::Check(!VerifyDeferredCells(graph, evaluation, {SemanticBudget{}.bytes, verify_work - 1}));
  const auto scopes = std::get<CellLifetimes>(build(evaluation.cells));
  const auto declared = std::get<CellScopeId>(scopes.cells[0]);
  const auto allocation = std::get<AllocatedCell>(evaluation.cells.cells[0].storage).allocation;
  test::Check(scopes.blocks[graph.ops[allocation.value].block.value] != declared);
  test::Check(scopes.blocks[graph.ops[store.value].block.value] == declared);
  test::Check(scopes.blocks[graph.ops[creation.value].block.value] == declared);
  auto broken = control;
  test::Check(broken.regions[0].has_value());
  broken.regions[0]->handler = broken.regions[0]->body;
  test::Check(std::get<CellLifetimeError>(BuildPhasedCellLifetimes(graph, *evaluation.environments,
      evaluation.cells, {raw.body, input, regions, broken}, {}, nullptr, nullptr, evaluation.deferred)) == CellLifetimeError::kControl);
  auto external = evaluation.cells; external.demands[0].creation = outside;
  test::Check(std::get<CellLifetimeError>(build(external)) == CellLifetimeError::kScopeEscape);
  auto wrong = evaluation; wrong.deferred[0] = creation;
  test::Check(!VerifyDeferredCells(graph, wrong, {}));
  // A native initializer on a different storage/store must never be accepted.
  wrong = evaluation; wrong.cells.cells[0].storage = IncomingCell{{0}, {0}};
  test::Check(!VerifyDeferredCells(graph, wrong, {}));
  std::size_t lo = 0, hi = SemanticBudget{}.bytes;
  while (lo < hi) { const auto mid = lo + (hi - lo) / 2;
    if (std::holds_alternative<CellLifetimes>(build(evaluation.cells, {mid}))) hi = mid; else lo = mid + 1; }
  test::Check(std::holds_alternative<CellLifetimes>(build(evaluation.cells, {lo, scopes.work})));
  test::Check(std::get<CellLifetimeError>(build(evaluation.cells, {lo - 1, scopes.work})) == CellLifetimeError::kResourceLimit);
  test::Check(std::get<CellLifetimeError>(build(evaluation.cells, {lo, scopes.work - 1})) == CellLifetimeError::kWorkLimit);
  std::cout << "native width=" << width << " copies=" << copies << " exact=" << lo << '/' << scopes.work << '\n';
}

}
int main() {
  for (auto n : {1U, 200U, 201U, 400U, 800U}) { Run(n, false); Run(n, true); Native(n, false); Native(n, true); }
  Native(1, false, true); Native(1, true, true);
  auto reset = Make(1, false, true);
  const auto locals = std::get<ExceptionLocals>(ProveExceptionLocals(reset.flow, reset.environments, {}, {}, ClosureWrites::kLanguage));
  // Two direct stores: the first dominates the second, so it is the
  // declaring store and the later one is an assignment (Language writes, no
  // reset to Hole). Before 2026-10-23 a unique store was required.
  test::Check(locals.deferred_initializations.size() == 1);
  const auto& deferred = locals.deferred_initializations[0];
  std::uint32_t first_store = UINT32_MAX;
  for (const auto& store : locals.plan.stores)
    if (store.cell.value == deferred.cell.value) first_store = std::min(first_store, store.instruction.value);
  test::Check(deferred.store.value == first_store);
}
