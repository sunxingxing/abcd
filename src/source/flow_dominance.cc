#include "flow_dominance.h"
#include "tree_checks.h"
#include <limits>
#include <stdexcept>
namespace abcd::source {
std::variant<FlowDominance,TreeStructureError> AnalyzeFlowDominance(
    const RecoveryFlow& flow,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);
    constexpr auto limit=std::numeric_limits<std::uint32_t>::max();
    RequireTree(flow.events.size()<limit && flow.edges.size()<limit,TreeStructureError::kResourceLimit);
    std::vector<std::uint32_t> offsets;
    std::vector<analysis::BlockId> successors;
    budget.Array(offsets,flow.events.size()+1);budget.Array(successors,flow.edges.size());
    std::size_t count=0;
    for(std::size_t i=0;i<flow.events.size();++i) {
      budget.Step();offsets[i]=static_cast<std::uint32_t>(count);
      for(auto edge=flow.events[i].outgoing;edge;edge=flow.edges[*edge].outgoing) {
        budget.Step();RequireTree(count<successors.size(),TreeStructureError::kTarget);
        successors[count++]={flow.edges[*edge].to.value};
      }
    }
    offsets.back()=static_cast<std::uint32_t>(count);successors.resize(count);
    const auto remaining=budget.Remaining();
    auto analyzed=analysis::BuildDominators({offsets,successors},{flow.entry.value},{remaining.bytes,remaining.work});
    if(const auto* error=std::get_if<analysis::DominatorError>(&analyzed)) {
      switch(*error) {
        case analysis::DominatorError::kEmptyGraph: case analysis::DominatorError::kInvalidEntry:
        case analysis::DominatorError::kInvalidEdge: return TreeStructureError::kTarget;
        case analysis::DominatorError::kWorkLimit:return TreeStructureError::kWorkLimit;
        case analysis::DominatorError::kResourceLimit:return TreeStructureError::kResourceLimit;
      }
    }
    auto tree=std::get<analysis::DominatorTree>(std::move(analyzed));
    budget.Admit(tree.work.allocated_bytes,1);budget.Step(tree.work.steps);
    return FlowDominance{std::move(tree),budget.Bytes(),budget.Work()};
  } catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
