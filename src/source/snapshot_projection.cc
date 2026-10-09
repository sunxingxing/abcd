#include "snapshot_projection.h"
#include "declaration_retirement.h"
#include "tree_checks.h"
#include <stdexcept>
namespace abcd::source {
BindingPlanView SnapshotProjection::View(BindingPlanView parent) const {
  parent.statements=statements;parent.omitted=omitted;
  parent.declaration_values=values;parent.declaration_completions=completions;
  parent.replacements=replacements;parent.forwarded_values=forwarded_values;
  parent.transports=transports;return parent;
}
void SnapshotProjection::Publish(BindingInitializers& plan) && {
  plan.statements=std::move(statements);plan.omitted=std::move(omitted);
  plan.declaration_values=std::move(values);plan.declaration_completions=std::move(completions);
  plan.replacements=std::move(replacements);plan.forwarded_values=std::move(forwarded_values);
  plan.transports=std::move(transports);
}
std::variant<SnapshotProjection,TreeStructureError> PlanSnapshotProjection(
    BindingPlanView current,std::span<const SnapshotMove> moves,
    std::span<const SnapshotSelection> selections,PrimitiveId undefined,
    analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);SnapshotProjection result;
    const auto statements=current.statements.size(),expressions=current.replacements.size();
    RequireTree(selections.size()==moves.size()&&current.omitted.size()==statements&&
        current.forwarded_values.size()==statements&&current.declaration_values.size()==statements&&
        current.declaration_completions.size()==statements&&current.transports.size()==expressions&&
        undefined.value<current.primitives.size(),TreeStructureError::kOwnership);
    const auto copy=[&]<class T>(std::vector<T>& output,std::span<const T> input) {
      budget.Array(output,input.size());budget.Step(input.size());
      std::copy(input.begin(),input.end(),output.begin());
    };
    copy(result.statements,current.statements);copy(result.omitted,current.omitted);
    copy(result.values,current.declaration_values);copy(result.completions,current.declaration_completions);
    copy(result.replacements,current.replacements);copy(result.forwarded_values,current.forwarded_values);
    copy(result.transports,current.transports);budget.Array(result.transported_reads,expressions);
    for(std::size_t i=0;i<moves.size();++i) {
      budget.Step();const auto& move=moves[i];
      RequireTree(move.binding.value<current.aliases.size()&&move.declaration.value<statements&&
          move.statement.value<statements&&move.read.value<expressions&&move.value.value<expressions&&
          (!move.expression||move.expression->value<expressions)&&
          (!move.initialization||move.initialization->value<statements),TreeStructureError::kOwnership);
      switch(selections[i]) {
        case SnapshotSelection::kRetain:continue;
        case SnapshotSelection::kForward:break;
        default:throw TreeStructureError::kOwnership;
      }
      result.replacements[move.read.value]=move.value;result.transported_reads[move.read.value]=move.binding;
      if(move.expression)result.transports[move.expression->value]=ExpressionTransport{move.value,undefined};
      else result.forwarded_values[move.statement.value]=move.value;
      RetireDeclaration(move.declaration,result.omitted,result.values,result.completions);
      if(move.initialization&&*move.initialization!=move.statement&&result.completions[move.initialization->value])
        RetireDeclaration(*move.initialization,result.omitted,result.values,result.completions);
      if(!move.expression)result.omitted[move.statement.value]=1;
      // A nested store can share another binding's initializer occurrence.
      // Identity owns completion retirement, not the enclosing syntax shape.
      auto& declaration=result.statements[move.statement.value];
      if(declaration&&declaration->symbol==move.binding)declaration.reset();
      auto& completion=result.completions[move.statement.value];
      if(completion&&completion->binding==move.binding)completion.reset();
    }
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
