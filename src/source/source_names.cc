#include "source_names.h"
#include "tree_checks.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace abcd::source {
std::variant<SourceNameFacts,TreeStructureError> AnalyzeSourceNames(
    const RecoveryFlow& flow,std::span<const SourceBindingPresence> presence,
    std::span<const SourceBindingShadow> canonical,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);SourceNameFacts result;const auto count=flow.bindings.size();
    RequireTree(presence.size()==count&&flow.source_spellings.size()==count,TreeStructureError::kOwnership);
    budget.Array(result.index.precision,count);
    for(auto& precision:result.index.precision){budget.Step();precision=SourceNameResolution::kResolved;}
    for(const auto spelling:flow.source_spellings){budget.Step();RequireTree(spelling.value<count,TreeStructureError::kOwnership);}
    for(const auto spelling:flow.source_name_spellings){budget.Step();RequireTree(!spelling||spelling->value<count,TreeStructureError::kOwnership);}
    std::vector<std::optional<SymbolId>> parents,heads,next,nearest;
    std::vector<SymbolId> ready;std::vector<std::size_t> depth;
    budget.Array(parents,count);budget.Array(heads,count);budget.Array(next,count);budget.Array(nearest,count);
    budget.Array(ready,count);budget.Array(depth,count);
    for(const auto& shadow:canonical) {
      budget.Step();RequireTree(shadow.binding.value<count,TreeStructureError::kOwnership);
      if(shadow.region) {
        RequireTree(shadow.region->value<flow.source_regions.size(),TreeStructureError::kOwnership);
        const auto region=flow.source_regions[shadow.region->value];
        RequireTree(region.first<region.last,TreeStructureError::kOwnership);
      }
      if(!shadow.shadowing){result.index.precision[flow.source_spellings[shadow.binding.value].value]=SourceNameResolution::kUnmodeled;continue;}
      const auto child=*shadow.shadowing;
      RequireTree(child.value<count&&child!=shadow.binding&&!parents[child.value]&&
          flow.source_spellings[child.value]==flow.source_spellings[shadow.binding.value],TreeStructureError::kOwnership);
      parents[child.value]=shadow.binding;next[child.value]=heads[shadow.binding.value];heads[shadow.binding.value]=child;
    }
    std::size_t queued=0;
    for(std::size_t i=0;i<count;++i){budget.Step();if(!parents[i])ready[queued++]={static_cast<std::uint32_t>(i)};}
    for(std::size_t at=0;at<queued;++at) {
      budget.Step();const auto node=ready[at];const auto parent=parents[node.value];
      depth[node.value]=parent?depth[parent->value]+1:0;
      switch(presence[node.value]) {
        case SourceBindingPresence::kRetained:nearest[node.value]=node;break;
        case SourceBindingPresence::kElided:nearest[node.value]=parent?nearest[parent->value]:std::nullopt;break;
        default:throw TreeStructureError::kOwnership;
      }
      for(auto child=heads[node.value];child;child=next[child->value]) {
        budget.Step();RequireTree(queued<count,TreeStructureError::kOwnership);ready[queued++]=*child;
      }
    }
    RequireTree(queued==count,TreeStructureError::kOwnership);
    struct BindingRange {SymbolId binding;SourceSpellingId spelling;SourceRegion region;bool external=false;};
    std::vector<BindingRange> bindings;std::vector<std::size_t> stack;
    budget.Array(bindings,count);budget.Array(stack,count);std::size_t retained=0;
    for(std::size_t i=0;i<count;++i) {
      budget.Step();if(presence[i]==SourceBindingPresence::kElided)continue;
      const SymbolId binding{static_cast<std::uint32_t>(i)};const auto spelling=flow.source_spellings[i];
      if(const auto region=flow.BindingRegion(binding)) {
        RequireTree(region->value<flow.source_regions.size(),TreeStructureError::kOwnership);
        const auto range=flow.source_regions[region->value];RequireTree(range.first<range.last,TreeStructureError::kOwnership);
        bindings[retained++]={binding,spelling,range,false};
      } else if(std::holds_alternative<EnclosingBindingScope>(flow.bindings[i].scope)) {
        // The adapter explicitly proves an enclosing environment.
        // Canonical parent identity orders self/capture/module/parameter layers;
        // they cover every body position without guessing an AST container.
        bindings[retained++]={binding,spelling,{{0},{std::numeric_limits<std::size_t>::max()}},true};
      } else result.index.precision[spelling.value]=SourceNameResolution::kUnmodeled;
    }
    bindings.resize(retained);
    std::sort(bindings.begin(),bindings.end(),[&](const auto& a,const auto& b){
      budget.Step();if(a.spelling!=b.spelling)return a.spelling<b.spelling;
      if(a.region.first!=b.region.first)return a.region.first<b.region.first;
      if(a.region.last!=b.region.last)return a.region.last>b.region.last;
      if(a.external!=b.external)return a.external;
      if(depth[a.binding.value]!=depth[b.binding.value])return depth[a.binding.value]<depth[b.binding.value];
      return a.binding<b.binding;
    });
    RequireTree(count<=std::numeric_limits<std::size_t>::max()/2,TreeStructureError::kResourceLimit);
    budget.Array(result.index.ranges,count*2);
    RequireTree(canonical.size()<=std::numeric_limits<std::size_t>::max()-count,TreeStructureError::kResourceLimit);
    budget.Array(result.shadows,count+canonical.size());std::size_t spans=0,shadows=0,pending=0;
    SourcePosition cursor{};std::optional<SourceSpellingId> spelling;
    const auto emit=[&](SourcePosition end) {
      budget.Step();if(pending&&cursor<end) {
        RequireTree(spans<result.index.ranges.size(),TreeStructureError::kOwnership);
        const auto& binding=bindings[stack[pending-1]];result.index.ranges[spans++]={binding.spelling,{cursor,end},binding.binding};
      }
      cursor=end;
    };
    const auto finish=[&]() {while(pending){budget.Step();emit(bindings[stack[pending-1]].region.last);--pending;}};
    for(std::size_t i=0;i<bindings.size();++i) {
      budget.Step();const auto& binding=bindings[i];
      if(spelling!=binding.spelling){finish();spelling=binding.spelling;cursor=binding.region.first;}
      while(pending&&bindings[stack[pending-1]].region.last<=binding.region.first) {
        budget.Step();emit(bindings[stack[pending-1]].region.last);--pending;
      }
      emit(binding.region.first);
      if(pending) {
        const auto& parent=bindings[stack[pending-1]];
        // Lexical intervals are nested or disjoint by scope semantics. Crossing
        // intervals are invalid adapter provenance, never a compiler pattern.
        RequireTree(binding.region.last<=parent.region.last,TreeStructureError::kOwnership);
        if(binding.region.first==parent.region.first&&binding.region.last==parent.region.last) {
          const auto original_parent=parents[binding.binding.value];
          const auto retained_parent=original_parent?nearest[original_parent->value]:std::nullopt;
          if((!binding.external&&!parent.external)||(binding.external&&parent.external&&retained_parent!=parent.binding))
            result.index.precision[binding.spelling.value]=SourceNameResolution::kAmbiguous;
        }
        result.shadows[shadows++]={parent.binding,flow.BindingRegion(binding.binding),binding.binding};
      }
      stack[pending++]=i;
    }
    finish();
    for(const auto& shadow:canonical){budget.Step();if(!shadow.shadowing)result.shadows[shadows++]=shadow;}
    result.index.ranges.resize(spans);result.shadows.resize(shadows);
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
std::variant<SourceNameResults,TreeStructureError> ResolveSourceNames(
    const RecoveryFlow& flow,std::span<const SourceNameQuery> queries,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);SourceNameResults result;budget.Array(result.resolutions,queries.size());
    for(std::size_t i=0;i<queries.size();++i) {
      budget.Step();const auto& query=queries[i];auto& resolution=result.resolutions[i];resolution=SourceNameResolution::kUnmodeled;
      if(!flow.source_names)continue;
      const auto& index=*flow.source_names;std::optional<SourceSpellingId> spelling;
      const auto* symbol=std::get_if<SymbolId>(&query.name);
      if(symbol){RequireTree(symbol->value<flow.source_spellings.size(),TreeStructureError::kOwnership);spelling=flow.source_spellings[symbol->value];}
      else {const auto name=std::get<NameId>(query.name);RequireTree(name.value<flow.source_name_spellings.size(),TreeStructureError::kOwnership);spelling=flow.source_name_spellings[name.value];}
      if(!spelling){resolution=SourceNameResolution::kResolved;continue;}
      RequireTree(spelling->value<index.precision.size(),TreeStructureError::kOwnership);
      if(index.precision[spelling->value]!=SourceNameResolution::kResolved){resolution=index.precision[spelling->value];continue;}
      if(!query.owner)continue;
      RequireTree(query.owner->value<flow.source_occurrences.size(),TreeStructureError::kOwnership);
      const auto position=flow.source_occurrences[query.owner->value];if(!position)continue;
      const auto found=std::upper_bound(index.ranges.begin(),index.ranges.end(),std::pair{*spelling,*position},[&](const auto& key,const auto& range){
        budget.Step();return key < std::pair{range.spelling,range.region.first};});
      const SourceNameRange* active=nullptr;
      if(found!=index.ranges.begin()) {
        const auto& range=*(found-1);if(range.spelling==*spelling&&range.region.first<=*position&&*position<range.region.last)active=&range;
      }
      resolution=active?(symbol&&*symbol==active->binding?SourceNameResolution::kResolved:SourceNameResolution::kShadowed):
          symbol?SourceNameResolution::kUnavailable:SourceNameResolution::kResolved;
    }
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
