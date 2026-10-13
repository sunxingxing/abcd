#include "source_variables.h"
#include "binding_storage.h"
#include "original_flow.h"
#include "tree_checks.h"
#include <new>
#include <stdexcept>
namespace abcd::source {
std::variant<SourceVariables,TreeStructureError> PrepareSourceVariables(
    const FunctionTree& tree,analysis::SemanticBudget allowance,const OriginalFlow* original) {
  TreePrintTrace trace(true);
  TreePrintSiteScope site(TreePrintSite::kVariableProjection);
  TreePrintLocationScope location(tree);
  try {
    TreeCheckBudget budget(allowance);SourceVariables result;result.tree=&tree;result.original=original;
    if(original)CheckOriginalFlow(tree,*original);
    const auto consume=[&]<class T>(std::variant<T,TreeStructureError> planned) {
      if(const auto* error=std::get_if<TreeStructureError>(&planned))throw *error;
      auto facts=std::get<T>(std::move(planned));
      budget.Admit(facts.allocated_bytes,1);budget.Step(facts.work);return facts;
    };
    result.nullish=consume(PlanNullishSyntax(tree,budget.Remaining()));
    result.loops=consume(PlanLoopSyntax(tree,budget.Remaining()));
    result.bindings=consume(PlanBindingInitializers(tree,result.loops,result.nullish,budget.Remaining(),original));
    // The binding planner and its revision facts have been destroyed. Keep
    // its peak admission in Bytes(), but do not retain that scratch for every
    // subsequent family member. Syntax storage remains conservatively charged.
    const auto binding_storage=BindingStorage(result.bindings);
    RequireTree(binding_storage<=result.bindings.allocated_bytes,TreeStructureError::kOwnership);
    budget.Release(result.bindings.allocated_bytes-binding_storage);
    result.retained_bytes=budget.ActiveBytes();
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){trace.failure.cause=error;trace.Propagate();return error;}
  catch(const std::bad_alloc&){ PrintCharge(TreePrintBudgetKind::kHostAllocation,{},0,0);trace.Propagate();return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){ PrintCharge(TreePrintBudgetKind::kCapacityOverflow,{},0,0);trace.Propagate();return TreeStructureError::kResourceLimit;}
}
}
