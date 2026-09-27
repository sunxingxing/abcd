#include "module_guards.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using Error = ModuleGuardError;
void Require(bool condition, Error error) { if (!condition) throw error; }
class Correspondence {
 public:
  Correspondence(const FunctionInput& input, const analysis::ValueProgram& graph,
      std::uint64_t work) : input_(input), graph_(graph), work_(work) {}
  void Step() { Require(work_ != 0, Error::kWorkLimit); --work_; }
  std::uint64_t Remaining() const { return work_; }
  analysis::OpId Operation(analysis::InstructionId site) {
    Step();
    Require(site.value < input_.Input().instructions.size() &&
        site.value < graph_.input.sites.size() && graph_.input.sites[site.value], Error::kOrigin);
    const auto id = *graph_.input.sites[site.value];
    Require(id.value < graph_.ops.size(), Error::kOrigin);
    const auto& op = graph_.ops[id.value];
    Require(op.life == analysis::ValueLife::kLive && op.origin.value < graph_.origins.size(), Error::kOrigin);
    const auto& origin = graph_.origins[op.origin.value];
    const auto& original = input_.Input().instructions[site.value];
    Require(origin.site.value == site.value && origin.pc == original.pc &&
        origin.original == original.origin && origin.disposition == analysis::OriginDisposition::kRetained, Error::kOrigin);
    Require(op.normal == original.effects.normal && op.before_throw == original.effects.before_throw &&
        op.exceptional == original.effects.exceptional, Error::kEffects);
    return id;
  }
  GuardedModuleRead Pair(const analysis::ModuleReadFact& fact) {
    Step();
    // Initialized reads require an explicit enclosing-owner premise. Catch and
    // caller guarded variants require phase-aware exceptional IR (T09).
    switch (fact.proof) {
      case analysis::ModuleReadProof::kGuarded: break;
      case analysis::ModuleReadProof::kInitialized:
      case analysis::ModuleReadProof::kCallerGuarded:
      case analysis::ModuleReadProof::kCatchGuarded: throw Error::kUnsupportedProof;
    }
    const auto read = Operation(fact.instruction);
    Require(fact.instruction.value + std::uint64_t{1} < input_.Input().instructions.size(), Error::kGuard);
    const analysis::InstructionId guard_site{fact.instruction.value + 1};
    const auto guard = Operation(guard_site);
    const auto& load = graph_.ops[read.value];
    const auto& check = graph_.ops[guard.value];
    const auto* original = std::get_if<analysis::ModuleLoad>(&input_.Input().instructions[fact.instruction.value].operation);
    const auto* current = std::get_if<analysis::ValueModuleRead>(&load.operation);
    Require(original && current && original->space == current->space && original->slot == current->slot &&
        load.use_count == 0 && load.result && load.result->value < graph_.values.size(), Error::kRead);
    const auto* result = std::get_if<analysis::OpResult>(&graph_.values[load.result->value].origin);
    Require(result && result->op == read && graph_.values[load.result->value].life == analysis::ValueLife::kLive, Error::kRead);
    const auto* original_guard = std::get_if<analysis::HoleGuard>(&input_.Input().instructions[guard_site.value].operation);
    const auto* current_guard = std::get_if<analysis::ValueGuard>(&check.operation);
    const auto* name = original_guard ? std::get_if<analysis::HoleNameSlot>(&original_guard->name) : nullptr;
    Require(original_guard && std::holds_alternative<analysis::Accumulator>(original_guard->value) && name &&
        current_guard && current_guard->name == name->slot && check.use_count == 1 && !check.result &&
        check.first_use.value < graph_.uses.size(), Error::kGuard);
    const auto& use = graph_.uses[check.first_use.value];
    Require(std::get_if<analysis::OpId>(&use.owner) && std::get<analysis::OpId>(use.owner) == guard &&
        use.role == analysis::OperandRole::kGuardValue && use.purpose == analysis::ReadPurpose::kOperand &&
        std::get_if<analysis::ValueId>(&use.operand) && std::get<analysis::ValueId>(use.operand) == *load.result, Error::kGuard);
    // Exact adjacency forbids observing pre-check Hole state, introducing a
    // branch/handler boundary, or moving any effect into the future fused read.
    Require(load.block == check.block && std::uint64_t{load.order} + 1 == check.order &&
        std::uint64_t{read.value} + 1 == guard.value, Error::kOrder);
    return {read, guard};
  }
 private:
  const FunctionInput& input_;
  const analysis::ValueProgram& graph_;
  std::uint64_t work_;
};
}  // namespace
std::variant<std::uint64_t, ModuleGuardError> VerifyModuleGuards(
    const FunctionInput& input, const analysis::ValueProgram& graph,
    const ModuleGuards& plan, analysis::SemanticBudget budget) {
  try {
    Require(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, Error::kRevision);
    const auto& facts = input.Proof().module_reads;
    Require(plan.pairs.size() == facts.size(), Error::kCoverage);
    Correspondence checked(input, graph, budget.work);
    for (std::size_t i = 0; i < facts.size(); ++i) {
      checked.Step();
      Require(i == 0 || facts[i - 1].instruction < facts[i].instruction, Error::kCoverage);
      Require(plan.pairs[i] == checked.Pair(facts[i]), Error::kCoverage);
    }
    return budget.work - checked.Remaining();
  } catch (Error error) { return error; }
}
std::variant<ModuleGuards, ModuleGuardError> BuildModuleGuards(
    const FunctionInput& input, const analysis::ValueProgram& graph,
    analysis::SemanticBudget budget) {
  try {
    auto bytes = budget.bytes;
    Require(sizeof(ModuleGuards) <= bytes, Error::kResourceLimit); bytes -= sizeof(ModuleGuards);
    const auto& facts = input.Proof().module_reads;
    Require(facts.size() <= bytes / sizeof(GuardedModuleRead), Error::kResourceLimit);
    ModuleGuards plan{graph.revision, graph.cfg_revision};
    plan.pairs.reserve(facts.size());
    Require(plan.pairs.capacity() <= bytes / sizeof(GuardedModuleRead), Error::kResourceLimit);
    bytes -= plan.pairs.capacity() * sizeof(GuardedModuleRead);
    Correspondence checked(input, graph, budget.work);
    for (const auto& fact : facts) plan.pairs.push_back(checked.Pair(fact));
    const auto verified = VerifyModuleGuards(input, graph, plan, {bytes, checked.Remaining()});
    if (const auto* error = std::get_if<Error>(&verified)) return *error;
    plan.allocated_bytes = budget.bytes - bytes;
    plan.work = budget.work - checked.Remaining() + std::get<std::uint64_t>(verified);
    return plan;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
