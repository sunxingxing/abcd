#include "source_name_retirement.h"
#include "tree_checks.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>
namespace abcd::source {
std::variant<SourceNameRetirement,TreeStructureError> AnalyzeSourceNameRetirement(
    const RecoveryFlow& flow,std::span<const SourceRetirement> candidates,
    std::span<const SourceRetirementQuery> queries,std::span<const SourceRetentionDemand> demands,
    analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);SourceNameRetirement result;const auto count=flow.bindings.size();
    RequireTree(candidates.size()==count&&flow.binding_presence.size()==count,TreeStructureError::kOwnership);
    budget.Array(result.presence,count);
    for(std::size_t i=0;i<count;++i) {
      budget.Step();RequireTree(flow.binding_presence[i]==SourceBindingPresence::kRetained||flow.binding_presence[i]==SourceBindingPresence::kElided,TreeStructureError::kOwnership);
      switch(candidates[i]) {
        case SourceRetirement::kFixed:result.presence[i]=flow.binding_presence[i];break;
        case SourceRetirement::kProposed:result.presence[i]=SourceBindingPresence::kElided;break;
        default:throw TreeStructureError::kOwnership;
      }
    }
    const auto validate=[&](SymbolId binding,std::optional<SymbolId> condition) {
      budget.Step();RequireTree(binding.value<count&&candidates[binding.value]==SourceRetirement::kProposed&&(!condition||condition->value<count),TreeStructureError::kOwnership);
    };
    for(const auto& query:queries){validate(query.binding,query.retained_by);if(const auto* root=std::get_if<SymbolId>(&query.lookup.name))RequireTree(root->value<count&&candidates[root->value]==SourceRetirement::kFixed&&result.presence[root->value]==SourceBindingPresence::kRetained,TreeStructureError::kOwnership);}
    for(const auto& demand:demands)validate(demand.binding,demand.retained_by);
    if(!flow.source_names) {
      // Missing shared spelling facts are a precision gap. Retain storage;
      // neither syntax nor an optimizer-local name scan may fill that gap.
      for(std::size_t i=0;i<count;++i){budget.Step();if(candidates[i]==SourceRetirement::kProposed)result.presence[i]=SourceBindingPresence::kRetained;}
      result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
    }
    auto baseline_result=AnalyzeSourceNames(flow,result.presence,flow.source_shadows,budget.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&baseline_result))throw *error;
    auto baseline=std::get<SourceNameFacts>(std::move(baseline_result));budget.Admit(baseline.allocated_bytes,1);budget.Step(baseline.work);
    std::vector<SourceNameQuery> lookups;budget.Array(lookups,queries.size());
    for(std::size_t i=0;i<queries.size();++i){budget.Step();lookups[i]=queries[i].lookup;}
    auto resolution_result=ResolveSourceNames(flow,baseline.index,lookups,budget.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&resolution_result))throw *error;
    auto resolutions=std::get<SourceNameResults>(std::move(resolution_result));budget.Admit(resolutions.allocated_bytes,1);budget.Step(resolutions.work);
    struct QueryPosition {SourceSpellingId spelling;std::optional<SymbolId> root;SourcePosition position;std::size_t query;};
    struct Dependency {SymbolId binding;std::optional<std::size_t> next;};
    std::vector<QueryPosition> positions;
    std::vector<Dependency> dependencies;
    std::vector<std::optional<std::size_t>> heads;
    std::vector<SymbolId> queue;
    std::vector<std::uint8_t> failed,global_failure;
    std::vector<std::size_t> successor,peer_count,group_first,group_last;
    budget.Array(positions,queries.size());budget.Array(heads,count);budget.Array(queue,count);
    RequireTree(demands.size()<=std::numeric_limits<std::size_t>::max()-queries.size(),TreeStructureError::kResourceLimit);
    budget.Array(dependencies,demands.size()+queries.size());budget.Array(failed,queries.size());budget.Array(global_failure,count);
    RequireTree(queries.size()<std::numeric_limits<std::size_t>::max(),TreeStructureError::kResourceLimit);
    budget.Array(successor,queries.size()+1);budget.Array(peer_count,count);budget.Array(group_first,count);budget.Array(group_last,count);
    std::size_t queued=0,edges=0,position_count=0;
    const auto retain=[&](SymbolId binding) {
      budget.Step();if(result.presence[binding.value]==SourceBindingPresence::kRetained)return;
      RequireTree(candidates[binding.value]==SourceRetirement::kProposed&&queued<count,TreeStructureError::kOwnership);
      result.presence[binding.value]=SourceBindingPresence::kRetained;queue[queued++]=binding;
    };
    const auto demand=[&](SymbolId binding,std::optional<SymbolId> condition) {
      budget.Step();if(!condition||result.presence[condition->value]==SourceBindingPresence::kRetained)retain(binding);
      else {RequireTree(edges<dependencies.size(),TreeStructureError::kOwnership);dependencies[edges]={binding,heads[condition->value]};heads[condition->value]=edges++;}
    };
    const auto fail=[&](std::size_t query) {
      budget.Step();if(failed[query])return;failed[query]=1;demand(queries[query].binding,queries[query].retained_by);
    };
    for(std::size_t i=0;i<queries.size();++i) {
      budget.Step();if(resolutions.resolutions[i]!=SourceNameResolution::kResolved){fail(i);continue;}
      const auto& lookup=queries[i].lookup;std::optional<SourceSpellingId> spelling;std::optional<SymbolId> root;
      if(const auto* symbol=std::get_if<SymbolId>(&lookup.name)){root=*symbol;spelling=flow.source_spellings[symbol->value];}
      else spelling=flow.source_name_spellings[std::get<NameId>(lookup.name).value];
      if(!spelling)continue;
      RequireTree(lookup.owner&&lookup.owner->value<flow.source_occurrences.size()&&flow.source_occurrences[lookup.owner->value],TreeStructureError::kOwnership);
      positions[position_count++]={*spelling,root,*flow.source_occurrences[lookup.owner->value],i};
    }
    positions.resize(position_count);
    const auto key=[](const QueryPosition& query){return std::tuple{query.spelling,query.root,query.position};};
    std::sort(positions.begin(),positions.end(),[&](const auto& a,const auto& b){budget.Step();return key(a)<key(b);});
    for(std::size_t i=0;i<=position_count;++i){budget.Step();successor[i]=i;}
    for(std::size_t i=0;i<count;++i){budget.Step();group_first[i]=position_count;}
    for(std::size_t i=0;i<position_count;++i){budget.Step();const auto spelling=positions[i].spelling.value;group_first[spelling]=std::min(group_first[spelling],i);group_last[spelling]=i+1;}
    // Successor deletion visits each invalidated query once. Partitioning by
    // the baseline retained root skips deeper fixed bindings without walking
    // lexical ancestors or rescanning unaffected queries on every activation.
    const auto find=[&](std::size_t node) {
      auto root=node;
      while(successor[root]!=root){budget.Step();root=successor[root];}
      while(successor[node]!=node){budget.Step();const auto next=successor[node];successor[node]=root;node=next;}
      return root;
    };
    const auto invalidate=[&](std::size_t first,std::size_t last) {
      auto at=find(first);
      while(at<last){budget.Step();fail(positions[at].query);successor[at]=find(at+1);at=successor[at];}
    };
    const auto invalidate_spelling=[&](SourceSpellingId spelling) {
      budget.Step();if(global_failure[spelling.value])return;global_failure[spelling.value]=1;
      const auto first=group_first[spelling.value];if(first<position_count)invalidate(first,group_last[spelling.value]);
    };
    for(std::size_t i=0;i<count;++i) {
      budget.Step();if(result.presence[i]==SourceBindingPresence::kRetained&&candidates[i]==SourceRetirement::kFixed)
        if(const auto peer=baseline.index.scope_peer[i])++peer_count[peer->value];
    }
    for(const auto& entry:demands)demand(entry.binding,entry.retained_by);
    for(std::size_t at=0;at<queued;++at) {
      budget.Step();const auto binding=queue[at];const auto spelling=flow.source_spellings[binding.value];
      for(auto edge=heads[binding.value];edge;edge=dependencies[*edge].next){budget.Step();retain(dependencies[*edge].binding);}
      if(const auto peer=baseline.index.scope_peer[binding.value])if(++peer_count[peer->value]>1){invalidate_spelling(spelling);continue;}
      SourceRegion range{};
      if(const auto region=flow.BindingRegion(binding))range=flow.source_regions[region->value];
      else if(std::holds_alternative<EnclosingBindingScope>(flow.bindings[binding.value].scope)) {
        // Canonical enclosing order must be modeled in the original facts.
        // Unknown/incomparable virtual environments remain a precision gap.
        RequireTree(spelling.value<flow.source_names->precision.size(),TreeStructureError::kOwnership);
        if(flow.source_names->precision[spelling.value]!=SourceNameResolution::kResolved){invalidate_spelling(spelling);continue;}
        range={{0},{std::numeric_limits<std::size_t>::max()}};
      }else {invalidate_spelling(spelling);continue;}
      const auto root=baseline.index.enclosing[binding.value];
      const auto first=std::lower_bound(positions.begin(),positions.end(),std::tuple{spelling,root,range.first},[&](const auto& query,const auto& value){budget.Step();return key(query)<value;});
      const auto last=std::lower_bound(first,positions.end(),std::tuple{spelling,root,range.last},[&](const auto& query,const auto& value){budget.Step();return key(query)<value;});
      invalidate(first-positions.begin(),last-positions.begin());
    }
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
