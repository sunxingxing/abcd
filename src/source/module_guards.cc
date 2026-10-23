#include "module_guards.h"

#include <algorithm>
#include <bit>
#include <new>
#include <stdexcept>

#include "module_state_flow.h"
#include "parameter_expression.h"
#include "tree_checks.h"

namespace abcd::source {
bool CopyOnlyModuleGuardInterval(const analysis::ValueProgram& graph,
                                 analysis::OpId read, analysis::OpId guard) {
  if (guard.value <= read.value || guard.value >= graph.ops.size()) return false;
  const auto& load = graph.ops[read.value];
  const auto& check = graph.ops[guard.value];
  if (load.block != check.block || check.order <= load.order ||
      std::uint64_t{check.order} - load.order != std::uint64_t{guard.value} - read.value) return false;
  for (auto k = read.value + 1; k < guard.value; ++k) {
    const auto& op = graph.ops[k];
    if (op.block != load.block || !std::holds_alternative<analysis::ValueCopy>(op.operation) ||
        op.normal != analysis::LanguageEffect::kNone || op.before_throw != analysis::LanguageEffect::kNone ||
        op.exceptional != analysis::ExceptionalCompletion::kImpossible) return false;
  }
  return true;
}
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
      case analysis::ModuleReadProof::kNamespace: return Namespace(fact);
      case analysis::ModuleReadProof::kCallerGuarded:
      case analysis::ModuleReadProof::kCatchGuarded: throw Error::kUnsupportedProof;
    }
    const auto read = Operation(fact.instruction);
    Require(fact.guard && fact.guard->value > fact.instruction.value &&
        fact.guard->value < input_.Input().instructions.size(), Error::kGuard);
    const analysis::InstructionId guard_site = *fact.guard;
    const auto guard = Operation(guard_site);
    const auto& load = graph_.ops[read.value];
    const auto& check = graph_.ops[guard.value];
    const auto* original = std::get_if<analysis::ModuleLoad>(&input_.Input().instructions[fact.instruction.value].operation);
    const auto* current = std::get_if<analysis::ValueModuleRead>(&load.operation);
    Require(original && current && original->space == current->space && original->slot == current->slot && original->loading == current->loading && original->domain == current->domain &&
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
    Require(work_ >= std::uint64_t{guard.value} - read.value, Error::kWorkLimit);
    work_ -= std::uint64_t{guard.value} - read.value;
    Require(CopyOnlyModuleGuardInterval(graph_, read, guard), Error::kOrder);
    return {read, guard};
  }
  ModuleReadSite Namespace(const analysis::ModuleReadFact& fact) {
    const auto read = Operation(fact.instruction);
    const auto* load = std::get_if<analysis::ModuleLoad>(&input_.Input().instructions[fact.instruction.value].operation);
    const auto& op = graph_.ops[read.value];
    const auto* current = std::get_if<analysis::ValueModuleRead>(&op.operation);
    Require(!fact.store && load && current && load->request_namespace && current->request_namespace &&
        load->space == analysis::ModuleSpace::kExternal && current->space == load->space &&
        current->slot == load->slot && current->loading == load->loading && current->domain == load->domain &&
        !op.use_count && op.result && op.result->value < graph_.values.size(), Error::kRead);
    const auto* result = std::get_if<analysis::OpResult>(&graph_.values[op.result->value].origin);
    Require(result && result->op == read && graph_.values[op.result->value].domain.IsLanguage() &&
        graph_.values[op.result->value].life == analysis::ValueLife::kLive, Error::kRead);
    return {read, std::nullopt};
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
std::variant<std::uint64_t, ModuleGuardError> VerifyLazyModuleReads(
    const analysis::ValueProgram& graph, const ModuleGuards& plan, analysis::SemanticBudget budget) {
  // A discarded lazy result still runs dependency code. Require a source read
  // proof for every live operation, not merely for reads with Language users.
  // Both inputs are indexed by OpId: merge coverage without per-read scans.
  const auto work=std::uint64_t{graph.ops.size()}+2*std::uint64_t{plan.pairs.size()};
  if(work>budget.work)return Error::kWorkLimit;
  for(std::size_t i=1;i<plan.pairs.size();++i)
    if(plan.pairs[i-1].read>=plan.pairs[i].read)return Error::kCoverage;
  std::size_t next=0;
  for(std::size_t i=0;i<graph.ops.size();++i) {
    while(next<plan.pairs.size()&&plan.pairs[next].read.value<i)++next;
    const auto* read=std::get_if<analysis::ValueModuleRead>(&graph.ops[i].operation);
    if(graph.ops[i].life!=analysis::ValueLife::kLive||!read||(read->loading!=analysis::ModuleLoading::kLazy && read->domain!=analysis::ModuleDomain::kSendable))continue;
    if(next==plan.pairs.size()||plan.pairs[next].read.value!=i||!plan.pairs[next].guard)return Error::kReadProof;
  }
  return work;
}
std::variant<std::uint64_t, ModuleGuardError> VerifyModuleGuards(
    const FunctionInput& input, const analysis::ValueProgram& graph,
    const ModuleGuards& plan, analysis::SemanticBudget budget) {
  try {
    Require(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, Error::kRevision);
    const auto& facts = input.Proof().module_reads;
    if (plan.signature) {
      TreeCheckBudget q(budget);
      const auto& source = *plan.signature;
      Require(source.graph != nullptr, Error::kCoverage);
      const auto& raw = *source.graph;
      Require(graph.revision.value == raw.revision.value + 1 &&
                  graph.cfg_revision.value == raw.cfg_revision.value + 1,
              Error::kRevision);
      auto parameters =
          BuildParameterPlan(raw, source.certificate.formals, q.Remaining());
      if (const auto* e = std::get_if<ParameterPlanError>(&parameters))
        throw *e == ParameterPlanError::kResourceLimit ? Error::kResourceLimit
        : *e == ParameterPlanError::kWorkLimit ? Error::kWorkLimit
                                               : Error::kUnsupportedProof;
      const auto& p = std::get<ParameterPlan>(parameters);
      q.Admit(p.allocated_bytes, 1);
      q.Step(p.work);
      auto proof =
          VerifyParameterExpressions(raw, p, source.certificate, q.Remaining());
      if (const auto* e = std::get_if<ParameterExpressionFailure>(&proof))
        throw e->kind == ParameterExpressionError::kResourceLimit
            ? Error::kResourceLimit
        : e->kind == ParameterExpressionError::kWorkLimit
            ? Error::kWorkLimit
            : Error::kUnsupportedProof;
      const auto cost = std::get<ParameterExpressionCost>(proof);
      q.Admit(cost.allocated_bytes, 1);
      q.Step(cost.work);
      std::vector<std::uint8_t> moved;
      q.Array(moved, raw.ops.size());
      for (const auto& e : source.certificate.events) {
        q.Step();
        moved[e.operation.value] = 1;
      }
      const auto allowed = q.Remaining().work;
      Correspondence old(input, raw, q.Remaining());
      Correspondence current(input, graph, q.Remaining());
      std::size_t kept = 0, migrated = 0;
      for (const auto& fact : facts) {
        q.Step();
        const auto pair = old.Pair(fact);
        if (moved[pair.read.value]) {
          Require(migrated < plan.signature_pairs.size() &&
                      plan.signature_pairs[migrated++] == pair &&
                      (!pair.guard || moved[pair.guard->value]),
                  Error::kCoverage);
          const auto tombstone = [&](analysis::OpId id) {
            Require(
                id.value < graph.ops.size() &&
                    graph.ops[id.value].life == analysis::ValueLife::kRemoved &&
                    std::holds_alternative<analysis::ValueNop>(
                        graph.ops[id.value].operation) &&
                    graph.ops[id.value].origin == raw.ops[id.value].origin,
                Error::kOrigin);
          };
          tombstone(pair.read);
          if (pair.guard) tombstone(*pair.guard);
        } else
          Require(kept < plan.pairs.size() &&
                      plan.pairs[kept++] == current.Pair(fact),
                  Error::kCoverage);
      }
      Require(
          kept == plan.pairs.size() && migrated == plan.signature_pairs.size(),
          Error::kCoverage);
      q.Step(allowed - old.Remaining());
      q.Step(allowed - current.Remaining());
      const auto lazy = VerifyLazyModuleReads(graph, plan, q.Remaining());
      if (const auto* error = std::get_if<Error>(&lazy)) return *error;
      q.Step(std::get<std::uint64_t>(lazy));
      return q.Work();
    }
    Require(plan.pairs.size() == facts.size(), Error::kCoverage);
    Correspondence checked(input, graph, budget);
    for (std::size_t i = 0; i < facts.size(); ++i) {
      checked.Step();
      Require(i == 0 || facts[i - 1].instruction < facts[i].instruction, Error::kCoverage);
      Require(plan.pairs[i] == checked.Pair(facts[i]), Error::kCoverage);
    }
    const auto lazy=VerifyLazyModuleReads(graph,plan,{budget.bytes,checked.Remaining()});
    if(const auto* error=std::get_if<Error>(&lazy))return *error;
    return budget.work - checked.Remaining()+std::get<std::uint64_t>(lazy);
  } catch (TreeStructureError e) {
    return e == TreeStructureError::kWorkLimit ? Error::kWorkLimit
                                               : Error::kResourceLimit;
  } catch (Error error) {
    return error;
  }
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
std::variant<ModuleGuards, ModuleGuardError> BuildParameterModuleGuards(
    const FunctionInput& input, const analysis::ValueProgram& graph,
    std::shared_ptr<const ParameterExpressionInput> source,
    analysis::SemanticBudget budget) {
  try {
    Require(source && source->graph, Error::kUnsupportedProof);
    TreeCheckBudget q(budget);
    auto raw = BuildModuleGuards(input, *source->graph, q.Remaining());
    if (const auto* e = std::get_if<ModuleGuardError>(&raw)) return *e;
    auto plan = std::get<ModuleGuards>(std::move(raw));
    q.Admit(plan.allocated_bytes, 1);
    q.Step(plan.work);
    std::vector<std::uint8_t> moved;
    q.Array(moved, graph.ops.size());
    for (const auto& e : source->certificate.events) {
      q.Step();
      Require(e.operation.value < moved.size(), Error::kCoverage);
      moved[e.operation.value] = 1;
    }
    q.Admit(plan.pairs.size(), sizeof(ModuleReadSite));
    plan.signature_pairs.reserve(plan.pairs.size());
    q.Admit(plan.signature_pairs.capacity() - plan.pairs.size(),
            sizeof(ModuleReadSite));
    auto out = plan.pairs.begin();
    for (const auto& pair : plan.pairs) {
      q.Step();
      if (moved[pair.read.value])
        plan.signature_pairs.push_back(pair);
      else
        *out++ = pair;
    }
    plan.pairs.erase(out, plan.pairs.end());
    plan.signature = std::move(source);
    plan.revision = graph.revision;
    plan.cfg_revision = graph.cfg_revision;
    auto checked = VerifyModuleGuards(input, graph, plan, q.Remaining());
    if (auto* e = std::get_if<ModuleGuardError>(&checked)) return *e;
    q.Step(std::get<std::uint64_t>(checked));
    plan.allocated_bytes = q.Bytes();
    plan.work = q.Work();
    return plan;
  } catch (TreeStructureError e) {
    return e == TreeStructureError::kWorkLimit ? Error::kWorkLimit
                                               : Error::kResourceLimit;
  } catch (Error e) {
    return e;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  }
}
}  // namespace abcd::source
