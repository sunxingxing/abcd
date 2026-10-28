#include "module_guards.h"

#include <new>
#include <stdexcept>
#include "../analysis/phased_value_import.h"
#include "abcd/analysis/exception_modules.h"

namespace abcd::source {
namespace {
using Error = ModuleGuardError;
void Require(bool condition, Error error) { if (!condition) throw error; }
class PhasePairs {
 public:
  PhasePairs(const analysis::PhasedValueInput& input, const analysis::ValueProgram& graph,
             analysis::SemanticBudget budget) : initial_(budget), remaining_(budget), graph_(graph) {
    // The raw graph is not edited: original PC/effects, phase successors,
    // guard operand and result-Commit identity all need import correspondence.
    const auto checked = analysis::VerifyImportedPhases(graph, input, remaining_);
    if (const auto* failure = std::get_if<analysis::ValueImportFailure>(&checked)) {
      if (failure->kind == analysis::ValueImportError::kResourceLimit) throw Error::kResourceLimit;
      if (failure->kind == analysis::ValueImportError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kOrigin;
    }
    Step(std::get<analysis::PhasedImportProof>(checked).work);
    auto proved = analysis::ProveExceptionModules(input.flow, input.ssa, remaining_, input.proof.initialized_modules);
    if (const auto* failure = std::get_if<analysis::ExceptionModuleError>(&proved)) {
      if (*failure == analysis::ExceptionModuleError::kResourceLimit) throw Error::kResourceLimit;
      throw Error::kUnsupportedProof;
    }
    modules_ = std::get<analysis::ExceptionModules>(std::move(proved));
    Step(modules_.work_units); Admit(modules_.AllocatedBytes());
  }
  void Step(std::uint64_t work = 1) {
    Require(work <= remaining_.work, Error::kWorkLimit); remaining_.work -= work;
  }
  void Admit(std::size_t bytes) {
    Require(bytes <= remaining_.bytes, Error::kResourceLimit); remaining_.bytes -= bytes;
  }
  std::size_t Count() const { return modules_.reads.size(); }
  analysis::SemanticBudget Remaining() const { return remaining_; }
  std::uint64_t Work() const { return initial_.work - remaining_.work; }
  ModuleReadSite Pair(std::size_t index) {
    Step();
    const auto& fact = modules_.reads[index];
    switch (fact.proof) {
      case analysis::ModuleReadProof::kCallerGuarded:
      case analysis::ModuleReadProof::kCatchGuarded: break;
      case analysis::ModuleReadProof::kInitialized: {
        const auto site = fact.instruction.value;
        Require(site < graph_.input.sites.size() && graph_.input.sites[site], Error::kOrigin);
        const auto read = *graph_.input.sites[site];
        Require(read.value < graph_.ops.size(), Error::kRead);
        const auto& op = graph_.ops[read.value];
        const auto* current = std::get_if<analysis::ValueModuleRead>(&op.operation);
        Require(current && current->space == analysis::ModuleSpace::kLocal && op.result &&
            graph_.values[op.result->value].domain.IsLanguage() && !op.use_count, Error::kRead);
        return {read, {}};
      }
      case analysis::ModuleReadProof::kGuarded:
      case analysis::ModuleReadProof::kStored:
      case analysis::ModuleReadProof::kNamespace: throw Error::kUnsupportedProof;
    }
    const auto site = fact.instruction.value;
    Require(fact.guard && fact.guard->value > site && fact.guard->value < graph_.input.sites.size() &&
        graph_.input.sites[site] && graph_.input.sites[fact.guard->value], Error::kOrigin);
    return {*graph_.input.sites[site], *graph_.input.sites[fact.guard->value]};
  }
 private:
  analysis::SemanticBudget initial_, remaining_;
  const analysis::ValueProgram& graph_;
  analysis::ExceptionModules modules_;
};
}  // namespace
std::variant<std::uint64_t, ModuleGuardError> VerifyPhasedModuleGuards(
    const analysis::PhasedValueInput& input, const analysis::ValueProgram& graph,
    const ModuleGuards& plan, analysis::SemanticBudget budget) {
  try {
    Require(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, Error::kRevision);
    PhasePairs original(input, graph, budget);
    Require(plan.pairs.size() == original.Count(), Error::kCoverage);
    for (std::size_t i = 0; i < plan.pairs.size(); ++i)
      Require(plan.pairs[i] == original.Pair(i), Error::kCoverage);
    const auto lazy=VerifyLazyModuleReads(graph,plan,original.Remaining());
    if(const auto* error=std::get_if<Error>(&lazy))return *error;
    return original.Work()+std::get<std::uint64_t>(lazy);
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<ModuleGuards, ModuleGuardError> BuildPhasedModuleGuards(
    const analysis::PhasedValueInput& input, const analysis::ValueProgram& graph,
    analysis::SemanticBudget budget) {
  try {
    PhasePairs original(input, graph, budget);
    original.Admit(sizeof(ModuleGuards));
    Require(original.Count() <= original.Remaining().bytes / sizeof(ModuleReadSite), Error::kResourceLimit);
    ModuleGuards plan{graph.revision, graph.cfg_revision};
    plan.pairs.reserve(original.Count());
    Require(plan.pairs.capacity() <= original.Remaining().bytes / sizeof(ModuleReadSite), Error::kResourceLimit);
    original.Admit(plan.pairs.capacity() * sizeof(ModuleReadSite));
    for (std::size_t i = 0; i < original.Count(); ++i) plan.pairs.push_back(original.Pair(i));
    const auto checked = VerifyPhasedModuleGuards(input, graph, plan, original.Remaining());
    if (const auto* failure = std::get_if<Error>(&checked)) return *failure;
    original.Step(std::get<std::uint64_t>(checked));
    plan.work = original.Work();
    plan.allocated_bytes = sizeof(ModuleGuards) + plan.pairs.capacity() * sizeof(ModuleReadSite);
    return plan;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
