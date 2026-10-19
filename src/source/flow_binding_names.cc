#include "recovery_flow.h"
#include "tree_checks.h"
#include "source_names.h"
#include <algorithm>
#include <stdexcept>

namespace abcd::source {
std::variant<FlowBindingNames,TreeStructureError> AnalyzeFlowBindingNames(
    const RecoveryFlow& flow,std::span<const SourceBindingShadow> canonical,
    analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);FlowBindingNames result;
    budget.Array(result.presence,flow.bindings.size());
    for(std::size_t i=0;i<flow.bindings.size();++i) {
      budget.Step();const auto& binding=flow.bindings[i];
      result.presence[i]=SourceBindingPresence::kRetained;
      switch(binding.kind) {
        case BindingKind::kCompilerTemporary:
          switch(binding.exposure) {
            case FlowBindingExposure::kPrivate:result.presence[i]=SourceBindingPresence::kElided;break;
            case FlowBindingExposure::kObservable:break;
            default:throw TreeStructureError::kOwnership;
          }
          break;
        case BindingKind::kParameter:case BindingKind::kLocal:case BindingKind::kCatchParameter:
        case BindingKind::kClassInner:
        case BindingKind::kFunctionSelf:
        case BindingKind::kCapture:
        case BindingKind::kModule:
          break;
        default:throw TreeStructureError::kBinding;
      }
    }
    const auto retain=[&](SymbolId binding) {
      RequireTree(binding.value<result.presence.size(),TreeStructureError::kOwnership);
      result.presence[binding.value]=SourceBindingPresence::kRetained;
    };
    // Actual retained syntax owns names even when values are dead/unreachable.
    // Bare declaration initialization is a definition, not evidence of elision.
    for(const auto& definition:flow.definitions){budget.Step();retain(definition.binding);}
    for(const auto& reference:flow.references){budget.Step();retain(reference.binding);}
    bool identified=false;
    for(const auto& shadow:canonical) {
      budget.Step();RequireTree(shadow.binding.value<flow.bindings.size(),TreeStructureError::kOwnership);
      if(shadow.region) {
        RequireTree(shadow.region->value<flow.source_regions.size(),TreeStructureError::kOwnership);
        const auto region=flow.source_regions[shadow.region->value];
        RequireTree(!flow.source_occurrences.empty()&&region.first<region.last,TreeStructureError::kOwnership);
      }
      if(shadow.shadowing) {
        RequireTree(shadow.shadowing->value<flow.bindings.size()&&shadow.shadowing!=shadow.binding,
            TreeStructureError::kOwnership);
        identified=true;
      }
    }
    if(!flow.source_spellings.empty()||!flow.source_name_spellings.empty()) {
      auto analyzed=AnalyzeSourceNames(flow,result.presence,canonical,budget.Remaining());
      if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
      auto names=std::get<SourceNameFacts>(std::move(analyzed));
      budget.Admit(names.allocated_bytes,1);budget.Step(names.work);
      result.shadows=std::move(names.shadows);result.index=std::move(names.index);
    } else if(!identified) {
      budget.Array(result.shadows,canonical.size());
      budget.Step(canonical.size());std::copy(canonical.begin(),canonical.end(),result.shadows.begin());
    } else {
      budget.Array(result.shadows,canonical.size());
      std::vector<std::optional<SymbolId>> parents,heads,next,nearest;
      std::vector<SymbolId> ready;
      budget.Array(parents,flow.bindings.size());budget.Array(heads,flow.bindings.size());
      budget.Array(next,flow.bindings.size());budget.Array(nearest,flow.bindings.size());
      budget.Array(ready,flow.bindings.size());
      for(const auto& shadow:canonical) {
        budget.Step();if(!shadow.shadowing)continue;const auto child=*shadow.shadowing;
        // A canonical spelling binding has one nearest enclosing same-spelling
        // binding. This is semantic provenance, not a source/CFG admission shape.
        RequireTree(!parents[child.value],TreeStructureError::kOwnership);
        parents[child.value]=shadow.binding;next[child.value]=heads[shadow.binding.value];
        heads[shadow.binding.value]=child;
      }
      std::size_t queued=0;
      for(std::size_t i=0;i<parents.size();++i) {
        budget.Step();if(!parents[i])ready[queued++]={static_cast<std::uint32_t>(i)};
      }
      // Parent-before-child forest traversal memoizes the nearest retained
      // ancestor once. No per-binding ancestor walk, recursion or resweep.
      for(std::size_t at=0;at<queued;++at) {
        budget.Step();const auto binding=ready[at];
        nearest[binding.value]=result.presence[binding.value]==SourceBindingPresence::kRetained?
            std::optional{binding}:parents[binding.value]?nearest[parents[binding.value]->value]:std::nullopt;
        for(auto child=heads[binding.value];child;child=next[child->value]) {
          budget.Step();RequireTree(queued<ready.size(),TreeStructureError::kOwnership);ready[queued++]=*child;
        }
      }
      RequireTree(queued==ready.size(),TreeStructureError::kOwnership);
      std::size_t count=0;
      for(const auto& shadow:canonical) {
        budget.Step();if(!shadow.shadowing){result.shadows[count++]=shadow;continue;}
        if(result.presence[shadow.shadowing->value]==SourceBindingPresence::kElided)continue;
        if(const auto outer=nearest[shadow.binding.value])result.shadows[count++]={*outer,shadow.region,shadow.shadowing};
      }
      result.shadows.resize(count);
    }
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  } catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
