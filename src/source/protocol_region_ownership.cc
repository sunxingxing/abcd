#include "protocol_region_ownership.h"
#include "control_skeleton.h"
#include <algorithm>
#include <new>
#include <stdexcept>
namespace abcd::source {
std::variant<VerifiedProtocolRegionOwnership, CloseCompletionFailure> VerifyProtocolRegionOwnership(
    const abc::MethodBody& body, abc::ProfileId profile, const analysis::ValueProgram& graph,
    const analysis::PhasedValueInput& input, const ExceptionPlan& plan, IteratorKeys keys,
    IteratorProtocolScan scan, CloseCompletionSummary summary,
    analysis::SemanticBudget budget, SourceContract contract) {
  using namespace analysis;
  try {
    // This stage intentionally requires the external-Hole premise. Earlier
    // contracts retain ordinary exception control even for a coincidental proof.
    if (ExternalExceptionNoHole(contract) != ExternalExceptionPolicy::kNoHoleV1 ||
        scan.candidates.empty() || summary.entries.empty() || summary.regions.empty())
      return CloseCompletionFailure{CloseCompletionError::kInput};
    ControlQuota quota(budget);
    // Occurrence coverage is checked separately from flags: an omitted close
    // or acquisition cannot authorize partial projection of the method.
    std::vector<bool> acquisitions;
    std::vector<std::uint8_t> owned, excluded, retained, close_blocks, close_nodes;
    std::vector<std::pair<IteratorProtocolId, OpId>> regions;
    quota.Size(owned,plan.regions.size());quota.Size(excluded,plan.regions.size());quota.Size(retained,plan.regions.size());
    quota.Size(close_blocks,graph.blocks.size());quota.Size(close_nodes,input.flow.nodes.size());
    quota.Size(acquisitions, scan.candidates.size()); quota.Size(regions, summary.regions.size());
    for (std::size_t i=0; i<summary.regions.size(); ++i) {
      quota.Work(); const auto& r=summary.regions[i];
      if (r.protocol.value>=acquisitions.size() || !r.boundary || !r.outcomes ||
          r.boundary->missing || r.boundary->entry_coverage!=CloseProofState::kVerified ||
          r.boundary->port_coverage!=CloseProofState::kVerified ||
          r.boundary->private_state!=CloseProofState::kVerified ||
          r.boundary->region_coverage!=CloseProofState::kVerified)
        return CloseCompletionFailure{CloseCompletionError::kOwnership,r.protocol,r.read};
      for(const auto& member:r.members){quota.Work();if(member.node.value>=close_nodes.size())return CloseCompletionFailure{CloseCompletionError::kCorrespondence};close_nodes[member.node.value]=1;}
      acquisitions[r.protocol.value]=true; regions[i]={r.protocol,r.read};
      for (const auto id:r.boundary->enclosures) {
        quota.Work();if(id.value>=excluded.size())return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
        excluded[id.value]=true;retained[id.value]=true;
      }
      for(const auto id:r.boundary->regions) {
        quota.Work();if(id.value>=owned.size())return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
        owned[id.value]=true;
      }
    }
    std::sort(regions.begin(),regions.end(),[&](const auto& a,const auto& b){quota.Work();return a<b;});
    for(std::size_t i=1;i<regions.size();++i) {
      quota.Work(); if(regions[i-1]==regions[i])return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
    }
    for(std::size_t b=0;b<graph.phases.size();++b){quota.Work();close_blocks[b]=close_nodes[graph.phases[b].input.value];}
    std::vector<std::uint8_t> entered;quota.Size(entered,owned.size());std::copy(owned.begin(),owned.end(),entered.begin());
    for(std::uint32_t r=0;r<owned.size();++r){
      quota.Work();
      if(owned[r] && retained[r]) {
        CloseCompletionFailure failure{CloseCompletionError::kNested};failure.region=TryId{r};return failure;
      }
      excluded[r]|=owned[r];
    }
    ProtocolForwarding forwarding;
    bool ordinary=false;for(std::size_t r=0;r<owned.size();++r){quota.Work();ordinary|=!excluded[r] && !plan.layout.regions[r].inactive;}
    if(ordinary){
    std::vector<TryId> bodies;
    {std::size_t count=0;for(const auto& r:summary.regions){quota.Work();for(const auto& range:r.boundary->protected_ranges)count+=range.role==ProtectedRangeRole::kLoopBody;}
     quota.Size(bodies,count);count=0;for(const auto& r:summary.regions)for(const auto& range:r.boundary->protected_ranges)if(range.role==ProtectedRangeRole::kLoopBody)bodies[count++]=range.region;}
    auto forwarded=AnalyzeProtocolForwarding(graph,plan,excluded,close_blocks,quota.Remaining(),retained,entered,bodies);
    if(const auto* f=std::get_if<ForInFailure>(&forwarded)){
      CloseCompletionFailure failure{f->kind==ForInError::kWorkLimit?CloseCompletionError::kWorkLimit:f->kind==ForInError::kResourceLimit?CloseCompletionError::kResourceLimit:CloseCompletionError::kOwnership};
      if(f->block)for(std::uint32_t r=0;r<plan.regions.size();++r){quota.Work();if(plan.regions[r].handler==*f->block || (plan.regions[r].binding && plan.regions[r].binding->block==*f->block)){failure.region=TryId{r};break;}}
      return failure;
    }
    forwarding=std::get<ProtocolForwarding>(std::move(forwarded));
    quota.Admit(forwarding.allocated_bytes,1);quota.Work(forwarding.work);
    }
    for(bool present:acquisitions) {quota.Work();if(!present)return CloseCompletionFailure{CloseCompletionError::kOwnership};}
    for(const auto& e:summary.entries) {
      quota.Work();
      for(const auto& obligation:e.obligations) {
        quota.Work(); if(obligation.state!=CloseProofState::kVerified || obligation.missing)
          return obligation.missing.value_or(CloseCompletionFailure{CloseCompletionError::kOwnership,e.protocol,e.read});
      }
    }
    auto checked = VerifyCloseCompletion(body, profile, graph, input, plan, keys, scan, summary, quota.Remaining(), contract);
    if (const auto* failure = std::get_if<CloseCompletionFailure>(&checked)) return *failure;
    quota.Work(std::get<std::uint64_t>(checked));
    return VerifiedProtocolRegionOwnership(std::move(summary),std::move(keys),std::move(scan),std::move(forwarding),profile,contract,budget.work-quota.RemainingWork());
  } catch(ControlError error) {
    return CloseCompletionFailure{error==ControlError::kWorkLimit?CloseCompletionError::kWorkLimit:CloseCompletionError::kResourceLimit};
  } catch(const std::bad_alloc&) {return CloseCompletionFailure{CloseCompletionError::kResourceLimit};
  } catch(const std::length_error&) {return CloseCompletionFailure{CloseCompletionError::kResourceLimit};}
}
}
