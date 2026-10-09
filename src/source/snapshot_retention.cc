#include "snapshot_projection.h"
#include "tree_checks.h"
#include <limits>
#include <stdexcept>
namespace abcd::source {
std::variant<SnapshotRetention,TreeStructureError> PlanSnapshotRetention(
    const RecoveryFlow& current,const RecoveryFlow& draft,std::span<const SnapshotMove> moves,
    std::span<const SourceRetentionDemand> prerequisites,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);SnapshotRetention result;
    std::vector<SourceRetirement> candidates;
    std::vector<SourceRetirementQuery> queries;
    std::vector<SourceRetentionDemand> demands;
    budget.Array(candidates,current.bindings.size());budget.Array(result.selections,moves.size());
    RequireTree(draft.references.size()<=std::numeric_limits<std::size_t>::max()-draft.names.size(),TreeStructureError::kResourceLimit);
    budget.Array(queries,draft.references.size()+draft.names.size());
    // Scope, storage dependency and transport retention per lookup; two nested edges or
    // one missing-owner demand per move, plus supplied effect/unit obligations.
    constexpr std::size_t kLookupDemands=3,kMoveDemands=2;
    const auto remaining=std::numeric_limits<std::size_t>::max()-prerequisites.size();
    RequireTree(queries.size()<=remaining/kLookupDemands&&
        moves.size()<=(remaining-queries.size()*kLookupDemands)/kMoveDemands,TreeStructureError::kResourceLimit);
    budget.Array(demands,prerequisites.size()+queries.size()*kLookupDemands+moves.size()*kMoveDemands);
    for(const auto& move:moves) {
      budget.Step();RequireTree(move.binding.value<candidates.size()&&move.value.value<draft.expression_transports.size()&&
          candidates[move.binding.value]==SourceRetirement::kFixed,TreeStructureError::kOwnership);
      candidates[move.binding.value]=SourceRetirement::kProposed;
    }
    std::copy(prerequisites.begin(),prerequisites.end(),demands.begin());budget.Step(prerequisites.size());
    std::size_t query_count=0,demand_count=prerequisites.size();
    const auto retain=[&](SymbolId binding){demands[demand_count++]={binding,{}};};
    const auto lookup=[&](SourceNameQuery name,SymbolId root) {
      RequireTree(root.value<candidates.size()&&candidates[root.value]==SourceRetirement::kProposed,TreeStructureError::kOwnership);
      if(!name.owner){retain(root);return;}
      if(const auto* binding=std::get_if<SymbolId>(&name.name)) {
        RequireTree(binding->value<candidates.size(),TreeStructureError::kOwnership);
        if(!current.BindingInScope(*binding,name.owner))retain(root);
        if(candidates[binding->value]==SourceRetirement::kProposed) {
          // Restoring a transport makes its original reads required again.
          // Close that def-use dependency before another candidate's storage
          // can disappear; the unchanged occurrence needs no motion query.
          demands[demand_count++]={*binding,root};retain(root);return;
        }
      }
      queries[query_count++]={name,root,{}};
    };
    for(const auto& reference:draft.references) {
      budget.Step();const auto* expression=std::get_if<ExprId>(&reference.site);if(!expression)continue;
      RequireTree(expression->value<draft.expression_transports.size(),TreeStructureError::kOwnership);
      if(const auto root=draft.expression_transports[expression->value])lookup({reference.binding,reference.owner},*root);
    }
    for(const auto& name:draft.names) {
      budget.Step();RequireTree(name.site.value<draft.expression_transports.size(),TreeStructureError::kOwnership);
      if(const auto root=draft.expression_transports[name.site.value])lookup({name.name,name.owner},*root);
    }
    for(const auto& move:moves) {
      budget.Step();const auto root=draft.expression_transports[move.value.value];
      if(!root){retain(move.binding);continue;}
      RequireTree(root->value<candidates.size()&&candidates[root->value]==SourceRetirement::kProposed,TreeStructureError::kOwnership);
      if(*root!=move.binding) {
        demands[demand_count++]={move.binding,*root};demands[demand_count++]={*root,move.binding};
      }
    }
    queries.resize(query_count);demands.resize(demand_count);
    auto analyzed=AnalyzeSourceNameRetirement(current,candidates,queries,demands,budget.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
    const auto& retirement=std::get<SourceNameRetirement>(analyzed);
    budget.Admit(retirement.allocated_bytes,1);budget.Step(retirement.work);
    for(std::size_t i=0;i<moves.size();++i) {
      budget.Step();result.selections[i]=retirement.presence[moves[i].binding.value]==SourceBindingPresence::kRetained?
          SnapshotSelection::kRetain:SnapshotSelection::kForward;
    }
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
