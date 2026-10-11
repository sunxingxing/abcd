#include "module_guards.h"
#include "module_state_flow.h"

#include <new>
#include <stdexcept>
#include <algorithm>
#include <bit>

namespace abcd::source {
namespace {
using Error = ModuleGuardError;
void Require(bool condition, Error error) { if (!condition) throw error; }
class Correspondence {
 public:
  Correspondence(const FunctionInput& input, const analysis::ValueProgram& graph,
      analysis::SemanticBudget budget) : input_(input), graph_(graph), work_(budget.work) {
    if (std::any_of(input.Proof().module_reads.begin(), input.Proof().module_reads.end(),
        [](const auto& fact) { return fact.proof == analysis::ModuleReadProof::kStored; })) {
      auto built=BuildWorkingModuleState(graph,budget);
      if (const auto* error=std::get_if<Error>(&built)) throw *error;
      states_=std::get<analysis::ModuleInitialization>(std::move(built));
      Require(states_->work<=work_,Error::kWorkLimit);work_-=states_->work;
    }
  }
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
        op.exceptional == original.effects.exceptional &&
          op.exception_cause == original.effects.exception_cause, Error::kEffects);
    return id;
  }
  ModuleReadSite Pair(const analysis::ModuleReadFact& fact) {
    Step();
    // Normal initialized reads retain the genuine creation/store premise;
    // phase callers require the separate exceptional owner pipeline.
    Require(!fact.store || fact.proof == analysis::ModuleReadProof::kStored, Error::kUnsupportedProof);
    switch (fact.proof) {
      case analysis::ModuleReadProof::kGuarded: break;
      case analysis::ModuleReadProof::kInitialized:
      case analysis::ModuleReadProof::kStored: return Initialized(fact);
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
  ModuleReadSite Initialized(const analysis::ModuleReadFact& fact) {
    const auto read = Operation(fact.instruction);
    const auto& original = input_.Input().instructions[fact.instruction.value];
    const auto* load = std::get_if<analysis::ModuleLoad>(&original.operation);
    const auto& op = graph_.ops[read.value];
    const auto* current = std::get_if<analysis::ValueModuleRead>(&op.operation);
    Require(load && current && load->space == analysis::ModuleSpace::kLocal &&
        current->space == load->space && current->slot == load->slot && !op.use_count &&
        op.result && op.result->value < graph_.values.size(), Error::kRead);
    const auto* result = std::get_if<analysis::OpResult>(&graph_.values[op.result->value].origin);
    Require(result && result->op == read && !result->commit &&
        graph_.values[op.result->value].domain.IsLanguage() &&
        graph_.values[op.result->value].life == analysis::ValueLife::kLive, Error::kRead);
    if (fact.proof == analysis::ModuleReadProof::kStored) {
      Require(states_.has_value(), Error::kUnsupportedProof);
      const analysis::InstructionId site{read.value};
      const auto cost=std::bit_width(states_->states.size())+1+ModuleSlotSet::kPathWork;
      Require(cost<=work_,Error::kWorkLimit); work_-=cost;
      const auto found=std::lower_bound(states_->states.begin(),states_->states.end(),site,
          [](const auto& state,auto id) { return state.instruction<id; });
      Require(found!=states_->states.end() && found->instruction==site &&
          found->slots.Contains(load->slot),Error::kUnsupportedProof);
      if (fact.store) {
        const auto store=Operation(*fact.store);
        const auto* original=std::get_if<analysis::ModuleStore>(&input_.Input().instructions[fact.store->value].operation);
        const auto* current=std::get_if<analysis::ValueModuleWrite>(&graph_.ops[store.value].operation);
        Require(original && current && original->slot==load->slot && current->slot==original->slot,Error::kRead);
      }
      return {read, std::nullopt};
    }
    const auto premises = input_.InitializedModuleReads();
    // Indexed exact read sites, not one repeated scan per module load.
    Require(work_ >= std::bit_width(premises.size()) + 1, Error::kWorkLimit);
    work_ -= std::bit_width(premises.size()) + 1;
    const auto found = std::lower_bound(premises.begin(), premises.end(), fact.instruction,
        [](const auto& premise, auto sought) { return premise.instruction < sought; });
    Require(found != premises.end() && found->instruction == fact.instruction && found->slot == load->slot &&
        input_.HasOwner(), Error::kUnsupportedProof);
    return {read, std::nullopt};
  }
 private:
  const FunctionInput& input_;
  const analysis::ValueProgram& graph_;
  std::uint64_t work_;
  std::optional<analysis::ModuleInitialization> states_;
};
}  // namespace
std::variant<std::uint64_t, ModuleGuardError> VerifyModuleGuards(
    const FunctionInput& input, const analysis::ValueProgram& graph,
    const ModuleGuards& plan, analysis::SemanticBudget budget) {
  try {
    Require(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, Error::kRevision);
    const auto& facts = input.Proof().module_reads;
    Require(plan.pairs.size() == facts.size(), Error::kCoverage);
    Correspondence checked(input, graph, budget);
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
    Require(facts.size() <= bytes / sizeof(ModuleReadSite), Error::kResourceLimit);
    ModuleGuards plan{graph.revision, graph.cfg_revision};
    plan.pairs.reserve(facts.size());
    Require(plan.pairs.capacity() <= bytes / sizeof(ModuleReadSite), Error::kResourceLimit);
    bytes -= plan.pairs.capacity() * sizeof(ModuleReadSite);
    auto work = budget.work;
    {
      Correspondence checked(input, graph, {bytes,work});
      for (const auto& fact : facts) plan.pairs.push_back(checked.Pair(fact));
      work = checked.Remaining();
    }
    // Release candidate roots before independent current-graph verification;
    // both passes share one bounded scratch allowance, never two live copies.
    const auto verified = VerifyModuleGuards(input, graph, plan, {bytes, work});
    if (const auto* error = std::get_if<Error>(&verified)) return *error;
    plan.allocated_bytes = budget.bytes - bytes;
    plan.work = budget.work - work + std::get<std::uint64_t>(verified);
    return plan;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
