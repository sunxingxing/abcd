#include "source_variables.h"
#include "original_flow.h"
#include "tree_checks.h"
#include <new>
#include <stdexcept>
namespace abcd::source {
std::variant<SourceVariables,TreeStructureError> PrepareSourceVariables(
    const FunctionTree& tree,analysis::SemanticBudget allowance,const OriginalFlow* original) {
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
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
