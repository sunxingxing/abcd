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
    std::vector<bool> acquisitions, owned;
    std::vector<std::pair<IteratorProtocolId, OpId>> regions;
    quota.Size(owned,plan.regions.size());
    quota.Size(acquisitions, scan.candidates.size()); quota.Size(regions, summary.regions.size());
    for (std::size_t i=0; i<summary.regions.size(); ++i) {
      quota.Work(); const auto& r=summary.regions[i];
      if (r.protocol.value>=acquisitions.size() || !r.boundary || !r.outcomes ||
          r.boundary->missing || r.boundary->entry_coverage!=CloseProofState::kVerified ||
          r.boundary->port_coverage!=CloseProofState::kVerified ||
          r.boundary->private_state!=CloseProofState::kVerified ||
          r.boundary->region_coverage!=CloseProofState::kVerified)
        return CloseCompletionFailure{CloseCompletionError::kOwnership,r.protocol,r.read};
      acquisitions[r.protocol.value]=true; regions[i]={r.protocol,r.read};
      for(const auto id:r.boundary->regions) {
        quota.Work();if(id.value>=owned.size())return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
        owned[id.value]=true;
      }
    }
    std::sort(regions.begin(),regions.end(),[&](const auto& a,const auto& b){quota.Work();return a<b;});
    for(std::size_t i=1;i<regions.size();++i) {
      quota.Work(); if(regions[i-1]==regions[i])return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
    }
    // A remaining ordinary enclosure is not granted ownership. This stage
    // retains the original control route/refusal for mixed ordinary scopes;
    // it must never erase a user wrapper to make a protocol projection fit.
    for(std::uint32_t r=0;r<owned.size();++r) {
      quota.Work();if(!owned[r] && !plan.layout.regions[r].inactive) {
        CloseCompletionFailure failure{CloseCompletionError::kOwnership};failure.region=TryId{r};return failure;
      }
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
    return VerifiedProtocolRegionOwnership(std::move(summary),std::move(keys),std::move(scan),profile,contract,budget.work-quota.RemainingWork());
  } catch(ControlError error) {
    return CloseCompletionFailure{error==ControlError::kWorkLimit?CloseCompletionError::kWorkLimit:CloseCompletionError::kResourceLimit};
  } catch(const std::bad_alloc&) {return CloseCompletionFailure{CloseCompletionError::kResourceLimit};
  } catch(const std::length_error&) {return CloseCompletionFailure{CloseCompletionError::kResourceLimit};}
}
}
