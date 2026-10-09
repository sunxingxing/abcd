#include "loop_declarations.h"
#include "tree_checks.h"
#include <stdexcept>
namespace abcd::source {
std::variant<LoopDeclarations,TreeStructureError> PlanLoopDeclarations(
    const LoopSyntax& loops,BindingPlanView projection,
    std::span<const std::optional<StmtId>> declarations,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);LoopDeclarations result;
    const auto omitted=projection.omitted;
    const auto initializers=projection.statements;
    RequireTree(omitted.size()==initializers.size(),TreeStructureError::kOwnership);
    std::size_t loop_count=0;
    for(const auto& entry:loops.initializers){budget.Step();loop_count+=entry.has_value();}
    budget.Array(result.starts,loop_count);
    budget.Array(result.environments,loop_count);
    budget.Array(result.bindings,loops.initializer_roots.size());
    budget.Array(result.demoted,loops.initializer_roots.size());
    budget.Array(result.declarators,loops.initializer_roots.size());
    budget.Array(result.entries,loops.initializer_roots.size());
    std::size_t starts=0,demoted=0,declarators=0,entries=0,environments=0,bindings=0;
    for(std::size_t i=0;i<loops.initializers.size();++i) {
      budget.Step();const auto& entry=loops.initializers[i];if(!entry)continue;
      auto first=entry->end;bool declaration=false;
      while(first>entry->first) {
        budget.Step();const auto root=loops.initializer_roots[first-1].statement;
        if(!initializers[root.value] && !omitted[root.value])break;
        declaration |= !omitted[root.value] && initializers[root.value].has_value();--first;
      }
      const auto selected=declaration?std::optional{first}:std::nullopt;
      if(projection.loop_declarations[i]!=selected)
        result.starts[starts++]={StmtId{static_cast<std::uint32_t>(i)},selected};
      bool allocated=false;
      for(auto at=entry->first;at<entry->end;++at) {
        budget.Step();const auto root=loops.initializer_roots[at].statement;
        result.entries[entries++]={root,selected&&at>=*selected?std::optional{StmtId{static_cast<std::uint32_t>(i)}}:std::nullopt};
        const auto& initializer=initializers[root.value];
        if(at<first) {if(initializer)result.demoted[demoted++]=root;}
        else if(!omitted[root.value]) {
          RequireTree(initializer.has_value(),TreeStructureError::kOwnership);
          if(!allocated) {
            result.environments[environments++]={StmtId{static_cast<std::uint32_t>(i)},root};allocated=true;
          }
          result.bindings[bindings++]={StmtId{static_cast<std::uint32_t>(i)},initializer->symbol};
          const auto original=declarations[initializer->symbol.value];
          RequireTree(original.has_value(),TreeStructureError::kOwnership);
          if(initializer->declaration!=DeclarationKind::kLet || (!omitted[original->value] && (projection.declaration_values.empty() || !projection.declaration_values[original->value])))
            result.declarators[declarators++]={root,*original};
        }
      }
    }
    result.starts.resize(starts);result.demoted.resize(demoted);result.declarators.resize(declarators);
    result.entries.resize(entries);result.environments.resize(environments);result.bindings.resize(bindings);
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
