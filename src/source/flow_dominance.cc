#include "flow_dominance.h"
#include "tree_checks.h"
#include <limits>
#include <stdexcept>
namespace abcd::source {
std::variant<FlowInitializations,TreeStructureError> AnalyzeFlowInitializations(
    const RecoveryFlow& flow,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);FlowInitializations result;
    budget.Array(result.declarations,flow.bindings.size());
    for(const auto& definition:flow.definitions) {
      budget.Step();bool selected=false;
      switch(definition.kind) {
        case FlowDefinitionKind::kParameter: case FlowDefinitionKind::kCatch:
        case FlowDefinitionKind::kFunctionSelf:selected=true;break;
        case FlowDefinitionKind::kUndefined: case FlowDefinitionKind::kInitializer:
          selected=definition.declaration && definition.declaration==flow.bindings[definition.binding.value].declaration;break;
        case FlowDefinitionKind::kAssignment: case FlowDefinitionKind::kUpdate:
        case FlowDefinitionKind::kUndefinedStore:break;
      }
      if(selected)result.declarations[definition.binding.value]=definition.point;
    }
    auto analyzed=AnalyzeFlowDominance(flow,budget.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
    result.order=std::get<FlowDominance>(std::move(analyzed));
    budget.Admit(result.order.allocated_bytes,1);budget.Step(result.order.work);
    // Canonical scope ownership has one static allocation site, visited anew
    // on each entry. Allocation dominating initialization, and initialization
    // dominating a read, proves completion since that allocation even on loops.
    // Do not infer dynamic equality from these static identities.
    for(std::size_t i=0;i<flow.bindings.size();++i) {
      budget.Step();const auto* unit=std::get_if<ScopeBindingLifetime>(&flow.bindings[i].lifetime);
      if(std::holds_alternative<UnmodeledBindingLifetime>(flow.bindings[i].lifetime)||
          (unit&&result.declarations[i]&&!result.order.Dominates(unit->enter,*result.declarations[i])))
        result.declarations[i].reset();
    }
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  } catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
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
