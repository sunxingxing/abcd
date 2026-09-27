#include "../../../src/source/capture_plan.h"
#include "../../../src/source/capture_cell_targets.h"
#include "../../../src/analysis/phased_value_import.h"
#include "abcd/analysis/exception_inputs.h"
#include "abcd/analysis/exception_locals.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
void Run(abc::ProfileId profile, bool early_store, bool hole_store = false, bool prior_environment = false) {
  const auto op = [](abc::Opcode value) { return static_cast<std::uint8_t>(value); };
  constexpr std::uint8_t kArgument = 3, kCellCount = 1, kFirstCell = 0;
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kNEWLEXENV_IMM8), kCellCount,
      op(abc::Opcode::kLDA_V8), kArgument};
  if (prior_environment) bytes.insert(bytes.begin(),
      {op(abc::Opcode::kNEWLEXENV_IMM8), kCellCount, op(abc::Opcode::kPOPLEXENV)});
  if (hole_store) bytes.push_back(op(abc::Opcode::kLDHOLE));
  if (early_store) bytes.insert(bytes.end(), {op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), kFirstCell});
  if (hole_store) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kArgument});
  const auto call_pc = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {op(abc::Opcode::kCALLARG0_IMM8), 0});
  if (!early_store) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kArgument,
      op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), kFirstCell});
  bytes.insert(bytes.end(), {op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 0, 0, 0, op(abc::Opcode::kRETURN)});
  const auto catch_pc = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 0, 0, 0, op(abc::Opcode::kRETURN)});
  auto body = test::Body(bytes);
  constexpr std::uint32_t kCallBytes = 2, kHandlerBytes = 6;
  abc::TryRegion region; region.offset = {call_pc}; region.length = kCallBytes; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {catch_pc}; handler.length = kHandlerBytes;
  body.tries.push_back(region); body.handlers.push_back(handler);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 4, {true, true, false, true}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers));
  IdfQuota ssa_work{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, ssa_work));
  const auto env = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  const auto locals = std::get<ExceptionLocals>(ProveExceptionLocals(flow, env, {}, {}, ClosureWrites::kLanguage));
  const auto unknown = std::get<ExceptionLocals>(ProveExceptionLocals(flow, env));
  std::vector<CapturePreservation> preserved;
  for (const auto& instruction : flow.instructions)
    if (std::holds_alternative<FunctionCreation>(instruction.operation)) preserved.push_back({instruction.pc});
  const auto read_only = std::get<ExceptionLocals>(ProveExceptionLocals(flow, env, {}, preserved));
  std::vector<EntryValueFact> entries;
  for (std::size_t i = 0; i < ssa.Values().size(); ++i)
    if (std::holds_alternative<MethodEntry>(ssa.Values()[i].origin) &&
        std::holds_alternative<abc::RegisterId>(ssa.Values()[i].slot))
      entries.push_back({{static_cast<std::uint32_t>(i)}, ValueKinds::Language()});
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
      value_work, {}, PendingExceptionValues::kLanguage, locals.loads));
  const auto proof = VerifyExceptionInputs(flow, ssa, env, values);
  if (hole_store) {
    // A Language closure-write premise cannot legalize the parent's own hole
    // store. Initialization intervals alone are not input/source certificates.
    test::Check(std::get<ExceptionInputFailure>(proof).kind == ExceptionInputError::kInternalValue);
    return;
  }
  test::Check(std::holds_alternative<ExceptionInputProof>(proof));
  const auto graph = std::get<ValueProgram>(ImportPhasedValues(
      {flow, ssa, env, values, std::get<ExceptionInputProof>(proof), frame}));
  auto working_env = std::get<ValueEnvironments>(BuildPhasedValueEnvironments(graph));
  const auto working_cells = std::get<CellPlan>(BuildPhasedCellPlan(graph, working_env));
  // Conditional fragments, not invented file/source receipts. Public owner
  // admission is exercised separately with real file-backed children.
  RouteInputIdentity identity; identity.profile = profile;
  constexpr abc::EntityOffset kParent{300}, kChild{400}, kRecord{100};
  const auto child_body = test::Body({op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), kFirstCell, op(abc::Opcode::kRETURN)});
  const auto child_input = std::get<FunctionInput>(FunctionInput::Build(identity, kChild, kRecord,
      child_body, frame, InputCaptureMode::kConditional));
  const auto child_values = std::get<FunctionValues>(BuildFunctionValues(child_input, child_body, frame));
  std::size_t creations = 0;
  for (std::size_t n = 0; n < flow.nodes.size(); ++n) {
    const auto& node = flow.nodes[n];
    const auto& instruction = flow.instructions[node.instruction.value];
    const auto* creation = std::get_if<FunctionCreation>(&instruction.operation);
    if (!creation || node.phase != ExceptionPhase::kBefore) continue;
    ++creations;
    const ExceptionNodeId before{static_cast<std::uint32_t>(n)};
    const ScopeFacts scopes(ExceptionScopeFacts{flow, env, locals, before});
    const CreationBinding binding{instruction.pc, *creation, {{0}, {}}, OrdinaryCapture{*env.before[n]}};
    const CaptureUse use{{{0}, {0}}, CaptureDomain::kLanguage};
    const auto built = BuildCapturePlan(scopes, node.instruction, binding, {&use, 1});
    if (!early_store && instruction.pc.value == catch_pc) {
      test::Check(std::get<CaptureError>(built) == CaptureError::kScope);
      continue;
    }
    auto plan = std::get<CapturePlan>(built);
    test::Check(plan.writes == ClosureWrites::kLanguage);
    test::Check(std::get<LocalCell>(plan.bindings[0].cell).environment == EnvironmentId{prior_environment ? 1U : 0U});
    const CaptureParent parent{identity, kParent, graph, &working_env, &working_cells,
        flow.instructions, locals.plan.allocations, scopes};
    auto cells = std::get<CaptureCells>(BindCaptureTargets(parent, {child_input, child_values},
        node.instruction, binding, plan));
    test::Check(cells.targets.size() == 1 && std::get<ParentCell>(cells.targets[0].target).cell == CellId{0});
    const auto verify_cells = [&] { return VerifyCaptureTargets(parent, {child_input, child_values},
        node.instruction, binding, cells); };
    test::Check(std::holds_alternative<std::uint64_t>(verify_cells()));
    const auto saved_target = cells.targets[0].target;
    cells.targets[0].target = ParentCell{CellId{1}};
    test::Check(std::get<CaptureCellError>(verify_cells()) == CaptureCellError::kBinding);
    cells.targets[0].target = saved_target;
    ++working_env.revision.value;
    test::Check(std::get<CaptureCellError>(verify_cells()) == CaptureCellError::kRevision);
    --working_env.revision.value;
    auto allocations = locals.plan.allocations;
    const auto environment = std::get<LocalCell>(plan.bindings[0].cell).environment;
    allocations[environment.value] = node.instruction;
    auto wrong_parent = parent; wrong_parent.allocations = allocations;
    test::Check(std::get<CaptureCellError>(VerifyCaptureTargets(wrong_parent, {child_input, child_values},
        node.instruction, binding, cells)) == CaptureCellError::kCell);
    SemanticBudget cell_budget; cell_budget.work = cells.work;
    test::Check(std::holds_alternative<CaptureCells>(BindCaptureTargets(parent, {child_input, child_values},
        node.instruction, binding, plan, cell_budget)));
    --cell_budget.work;
    test::Check(std::get<CaptureCellError>(BindCaptureTargets(parent, {child_input, child_values},
        node.instruction, binding, plan, cell_budget)) == CaptureCellError::kWorkLimit);
    cell_budget = {}; cell_budget.bytes = 0;
    test::Check(std::get<CaptureCellError>(BindCaptureTargets(parent, {child_input, child_values},
        node.instruction, binding, plan, cell_budget)) == CaptureCellError::kResourceLimit);
    if (profile == abc::ProfileId::kDynamic1301 && early_store && !prior_environment && creations == 1) {
      // Many original reads AND writes alias one mutable cell. The verifier
      // must cover every occurrence, not accept only the first matching use.
      for (const std::size_t count : {200, 201, 402}) {
        std::vector<std::uint8_t> code;
        for (std::size_t i = 0; i < count; ++i) code.insert(code.end(), {
            op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), kFirstCell,
            op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), kFirstCell});
        code.push_back(op(abc::Opcode::kRETURNUNDEFINED));
        const auto repeated_body = test::Body(code);
        const auto repeated_input = std::get<FunctionInput>(FunctionInput::Build(identity, kChild, kRecord,
            repeated_body, frame, InputCaptureMode::kConditional));
        const auto repeated_values = std::get<FunctionValues>(BuildFunctionValues(repeated_input, repeated_body, frame));
        auto repeated_plan = std::get<CapturePlan>(BuildCapturePlan(scopes, node.instruction,
            binding, repeated_input.Captures()));
        auto targets = std::get<CaptureCells>(BindCaptureTargets(parent, {repeated_input, repeated_values},
            node.instruction, binding, std::move(repeated_plan)));
        constexpr std::size_t kUsesPerPair = 2;
        test::Check(targets.targets.size() == 1 && targets.input.bindings.size() == kUsesPerPair * count);
        test::Check(std::holds_alternative<std::uint64_t>(VerifyCaptureTargets(parent,
            {repeated_input, repeated_values}, node.instruction, binding, targets)));
        targets.input.bindings.pop_back();
        test::Check(std::get<CaptureCellError>(VerifyCaptureTargets(parent, {repeated_input, repeated_values},
            node.instruction, binding, targets)) == CaptureCellError::kInput);
      }
    }
    test::Check(std::holds_alternative<std::uint64_t>(VerifyCapturePlan(scopes, node.instruction, binding, {&use, 1}, plan)));
    plan.writes = ClosureWrites::kUnknown;
    test::Check(std::get<CaptureError>(VerifyCapturePlan(scopes, node.instruction, binding, {&use, 1}, plan)) == CaptureError::kScope);
    const ScopeFacts unproved(ExceptionScopeFacts{flow, env, unknown, before});
    test::Check(std::get<CaptureError>(BuildCapturePlan(unproved, node.instruction, binding, {&use, 1})) == CaptureError::kScope);
    const ScopeFacts unchanged(ExceptionScopeFacts{flow, env, read_only, before});
    const auto preserved_plan = std::get<CapturePlan>(BuildCapturePlan(unchanged, node.instruction, binding, {&use, 1}));
    test::Check(preserved_plan.writes == ClosureWrites::kUnknown);
    const ScopeFacts commit(ExceptionScopeFacts{flow, env, locals, ExceptionNodeId{before.value + 1}});
    test::Check(std::get<CaptureError>(BuildCapturePlan(commit, node.instruction, binding, {})) == CaptureError::kCreation);
    const CaptureUse defined{{{0}, {0}}, CaptureDomain::kDefined};
    test::Check(std::get<CaptureError>(BuildCapturePlan(scopes, node.instruction, binding, {&defined, 1})) == CaptureError::kScope);
    const CaptureUse outer{{{1}, {0}}, CaptureDomain::kDefined};
    const auto outward = std::get<CapturePlan>(BuildCapturePlan(scopes, node.instruction, binding, {&outer, 1}));
    test::Check(std::get<CapturedSlot>(outward.bindings[0].cell).depth.value == 0 &&
        outward.bindings[0].domain == CaptureDomain::kDefined);
    auto forged = std::get<CapturePlan>(built);
    std::get<LocalCell>(forged.bindings[0].cell).slot.value = 1;
    test::Check(std::get<CaptureError>(VerifyCapturePlan(scopes, node.instruction, binding, {&use, 1}, forged)) == CaptureError::kBinding);
    SemanticBudget empty; empty.bytes = 0;
    test::Check(std::get<CaptureError>(BuildCapturePlan(scopes, node.instruction, binding, {&use, 1}, empty)) == CaptureError::kResourceLimit);
    for (const std::size_t count : {200, 201, 4096}) {
      const std::vector<CaptureUse> uses(count, use);
      const auto many = std::get<CapturePlan>(BuildCapturePlan(scopes, node.instruction, binding, uses));
      SemanticBudget budget; budget.work = many.work;
      test::Check(std::holds_alternative<CapturePlan>(BuildCapturePlan(scopes, node.instruction, binding, uses, budget)));
      --budget.work;
      test::Check(std::get<CaptureError>(BuildCapturePlan(scopes, node.instruction, binding, uses, budget)) == CaptureError::kWorkLimit);
    }
  }
  test::Check(creations == 2);
}
}  // namespace
int main() {
  for (const auto profile : {abcd::abc::ProfileId::kDynamic1202, abcd::abc::ProfileId::kDynamic1206,
                            abcd::abc::ProfileId::kDynamic1301}) {
    Run(profile, true); Run(profile, false); Run(profile, true, true);
    Run(profile, true, false, true);
  }
}
