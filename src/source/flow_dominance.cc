#include "flow_dominance.h"
#include "tree_checks.h"
#include <limits>
#include <stdexcept>
namespace abcd::source {
std::variant<FlowDominance,TreeStructureError> AnalyzeFlowDominance(
    const RecoveryFlow& flow,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);analysis::NormalFlow graph;
    constexpr auto limit=std::numeric_limits<std::uint32_t>::max();
    RequireTree(flow.events.size()<limit && flow.edges.size()<limit-flow.events.size(),TreeStructureError::kResourceLimit);
    budget.Array(graph.blocks,flow.events.size()+flow.edges.size());
    const auto edge_point=[&](std::size_t edge){return analysis::BlockId{static_cast<std::uint32_t>(flow.events.size()+edge)};};
    for(std::size_t i=0;i<flow.events.size();++i) {
      budget.Step();if(const auto edge=flow.events[i].outgoing) {
        graph.blocks[i].successor_count=1;graph.blocks[i].successors[0].target=edge_point(*edge);
      }
    }
    for(std::size_t i=0;i<flow.edges.size();++i) {
      budget.Step();const auto& edge=flow.edges[i];auto& block=graph.blocks[edge_point(i).value];
      block.successor_count=1;block.successors[0].target={edge.to.value};
      if(edge.outgoing){block.successor_count=analysis::kMaxNormalSuccessors;block.successors[1].target=edge_point(*edge.outgoing);}
    }
    const auto remaining=budget.Remaining();
    auto analyzed=analysis::BuildNormalDominators(graph,{flow.entry.value},{remaining.bytes,remaining.work});
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
