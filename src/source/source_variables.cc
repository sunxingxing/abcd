#include "source_variables.h"
#include "original_flow.h"
#include "tree_checks.h"
#include <new>
#include <stdexcept>
namespace abcd::source {
namespace {
std::size_t BindingStorage(const BindingInitializers& plan) {
  // All owned binding-plan payloads are flat vectors (literal text is an ID).
  // Count capacity, not size; adding an owning field requires adding it here.
  const auto bytes=[]<class T>(const std::vector<T>& values){return values.capacity()*sizeof(T);};
  return bytes(plan.statements)+bytes(plan.omitted)+bytes(plan.statement_values)+
      bytes(plan.forwarded_values)+bytes(plan.loop_declarations)+bytes(plan.primitives)+
      bytes(plan.literals)+bytes(plan.read_literals)+bytes(plan.read_bindings)+bytes(plan.read_sources)+
      bytes(plan.aliases)+bytes(plan.storage)+bytes(plan.undefined_stores)+bytes(plan.declaration_values)+
      bytes(plan.declaration_completions)+bytes(plan.replacements)+bytes(plan.transports)+
      bytes(plan.evaluations)+bytes(plan.normal_stores)+bytes(plan.entry_counts)+bytes(plan.child_counts);
}
}
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
    // The binding planner and its revision facts have been destroyed. Keep
    // its peak admission in Bytes(), but do not retain that scratch for every
    // subsequent family member. Syntax storage remains conservatively charged.
    const auto binding_storage=BindingStorage(result.bindings);
    RequireTree(binding_storage<=result.bindings.allocated_bytes,TreeStructureError::kOwnership);
    budget.Release(result.bindings.allocated_bytes-binding_storage);
    result.retained_bytes=budget.ActiveBytes();
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
