#include "loop_entry_projection.h"
#include "tree_checks.h"
#include <algorithm>
#include <stdexcept>
namespace abcd::source {
std::variant<LoopEntryProjection,TreeStructureError> ProjectLoopEntryFlow(
    const LoopDeclarations& declarations,std::span<const std::optional<BindingInitializer>> initializers,
    std::span<const std::optional<DeclarationCompletion>> completions,
    std::span<const SourceBindingScope> original_bindings,const SourceScopeProjection& scopes,
    std::span<const LoopEntryOccurrence> entries,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);LoopEntryProjection result;
    RequireTree(scopes.occurrences.size()==initializers.size() && entries.size()==initializers.size() &&
        (completions.empty()||completions.size()==initializers.size()) &&
        original_bindings.size()==scopes.bindings.size() &&
        (scopes.allocations.empty()||scopes.allocations.size()==scopes.regions.size()),TreeStructureError::kOwnership);
    const auto copy=[&](auto& target,auto source){budget.Array(target,source.size());std::copy(source.begin(),source.end(),target.begin());};
    copy(result.initializers,initializers);copy(result.completions,completions);
    copy(result.bindings,scopes.bindings);copy(result.occurrences,scopes.occurrences);
    budget.Array(result.allocations,original_bindings.size());
    std::vector<std::optional<StmtId>> starts;budget.Array(starts,initializers.size());
    for(const auto& environment:declarations.environments) {
      budget.Step();RequireTree(environment.loop.value<starts.size() && environment.entry.value<starts.size(),TreeStructureError::kOwnership);
      RequireTree(!starts[environment.loop.value],TreeStructureError::kOwnership);
      starts[environment.loop.value]=environment.entry;
    }
    std::vector<std::uint8_t> prefix;budget.Array(prefix,initializers.size());
    for(const auto& entry:declarations.entries) {
      budget.Step();RequireTree(entry.statement.value<prefix.size(),TreeStructureError::kOwnership);
      if(entry.loop)continue;
      prefix[entry.statement.value]=1;
      if(const auto initializer=result.initializers[entry.statement.value]) {
        RequireTree(initializer->symbol.value<result.bindings.size(),TreeStructureError::kOwnership);
        result.bindings[initializer->symbol.value]=original_bindings[initializer->symbol.value];
        result.initializers[entry.statement.value].reset();
        if(!result.completions.empty())if(auto& completion=result.completions[entry.statement.value])
          completion->kind=DeclarationCompletionKind::kAssignUndefined;
      }
    }
    for(const auto& member:declarations.bindings) {
      budget.Step();RequireTree(member.binding.value<result.allocations.size() && member.loop.value<starts.size() && starts[member.loop.value].has_value(),TreeStructureError::kOwnership);
      RequireTree(!result.allocations[member.binding.value],TreeStructureError::kOwnership);
      result.allocations[member.binding.value]=NativeLoopAllocation{member.loop,*starts[member.loop.value]};
    }
    // One indexed ownership pass handles every descendant, including split
    // transparent blocks. Do not walk a subtree separately for each root.
    for(std::size_t i=0;i<entries.size();++i) {
      budget.Step();if(const auto root=entries[i].root) {
        RequireTree(root->value<prefix.size(),TreeStructureError::kOwnership);
        if(prefix[root->value])result.occurrences[i]=entries[i].enclosing;
      }
    }
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
