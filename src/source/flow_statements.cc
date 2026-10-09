#include "flow_statements.h"
#include "tree_checks.h"
#include <stdexcept>
namespace abcd::source {
std::variant<FlowStatements,TreeStructureError> AnalyzeFlowStatements(
    const RecoveryFlow& flow,const FlowCycles& cycles,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);FlowStatements result;
    const auto size=flow.statement_semantics.size();
    RequireTree(size==flow.statements.size(),TreeStructureError::kOwnership);
    RequireTree(cycles.cyclic.empty()||cycles.cyclic.size()==flow.events.size(),TreeStructureError::kOwnership);
    budget.Array(result.observations,size);
    std::vector<std::uint8_t> seen;budget.Array(seen,size);
    // Validate and seed in semantic traversal order; numeric IDs/arena order
    // are irrelevant. Parent publication must precede its owned children.
    for(const auto id:flow.statement_order) {
      budget.Step();RequireTree(id.value<size&&!seen[id.value]&&flow.statement_semantics[id.value],TreeStructureError::kOwnership);
      const auto& statement=*flow.statement_semantics[id.value];
      if(statement.parent)RequireTree(statement.parent->value<size&&seen[statement.parent->value],TreeStructureError::kOwnership);
      seen[id.value]=1;auto& observation=result.observations[id.value];
      switch(statement.completion) {
        case FlowStatementCompletion::kNormal:case FlowStatementCompletion::kJump:break;
        case FlowStatementCompletion::kAbrupt:observation.abrupt=true;break;
        case FlowStatementCompletion::kCleanup:observation.cleanup=true;break;
      }
      switch(statement.progress) {
        case FlowStatementProgress::kFinite:break;
        case FlowStatementProgress::kMayRepeat:
          if(statement.repeat_point) {
            RequireTree(statement.repeat_point->value<flow.events.size()&&cycles.cyclic.size()==flow.events.size(),TreeStructureError::kOwnership);
            observation.repetition=cycles.cyclic[statement.repeat_point->value]!=0;
          }else observation.repetition=true;
          break;
      }
    }
    for(std::size_t i=0;i<size;++i) {budget.Step();RequireTree(bool(flow.statement_semantics[i])==bool(seen[i]),TreeStructureError::kOwnership);}
    for(const auto& event:flow.events) {
      budget.Step();if(!event.owner)continue;
      RequireTree(event.owner->value<size&&seen[event.owner->value],TreeStructureError::kOwnership);
      auto& observation=result.observations[event.owner->value];
      if(event.effect==ExpressionEvaluation::kRequired)observation.evaluation=ExpressionEvaluation::kRequired;
      observation.definitions|=event.definition.has_value();
      observation.suspension|=event.kind==FlowEventKind::kSuspend;
    }
    // Resolve all local jump consumers through indexed semantic boundaries.
    // A forest activation stack gives the nearest owning normal exit in O(1)
    // per query; difference accumulation marks intervening regions, not the
    // consumer or its ancestors. No target set union or ancestor walk per jump.
    std::size_t count=0;
    for(const auto& edge:flow.edges) {
      budget.Step();RequireTree(edge.from.value<flow.events.size()&&edge.to.value<flow.events.size(),TreeStructureError::kTarget);
      const auto owner=flow.events[edge.from.value].owner;if(!owner)continue;
      RequireTree(owner->value<size&&seen[owner->value],TreeStructureError::kOwnership);
      auto& observation=result.observations[owner->value];
      switch(edge.completion) {
        case FlowCompletion::kNormal:case FlowCompletion::kTaken:case FlowCompletion::kNotTaken:break;
        case FlowCompletion::kJump:++count;break;
        case FlowCompletion::kThrow:case FlowCompletion::kReturn:observation.abrupt=true;break;
        case FlowCompletion::kResumeNext:observation.suspension=true;break;
        case FlowCompletion::kResumeThrow:case FlowCompletion::kResumeReturn:
          observation.suspension=true;observation.abrupt=true;break;
        case FlowCompletion::kConservativeCleanup:observation.cleanup=true;break;
      }
    }
    struct Jump {FlowPoint target;std::optional<std::size_t> next;};
    std::vector<Jump> jumps;budget.Array(jumps,count);
    std::vector<std::optional<std::size_t>> queries;budget.Array(queries,size);
    std::vector<std::int64_t> delta;budget.Array(delta,size);
    if(count) {
      std::vector<std::optional<StmtId>> children,siblings,exits,previous;
      budget.Array(children,size);budget.Array(siblings,size);budget.Array(previous,size);budget.Array(exits,flow.events.size());
      for(const auto id:flow.statement_order) {
        budget.Step();const auto& statement=*flow.statement_semantics[id.value];
        if(statement.normal_exit)RequireTree(statement.normal_exit->value<exits.size(),TreeStructureError::kTarget);
        if(statement.parent){siblings[id.value]=children[statement.parent->value];children[statement.parent->value]=id;}
      }
      std::size_t index=0;
      for(const auto& edge:flow.edges) {
        budget.Step();if(edge.completion!=FlowCompletion::kJump)continue;
        const auto owner=flow.events[edge.from.value].owner;if(!owner)continue;
        jumps[index]={edge.to,queries[owner->value]};queries[owner->value]=index++;
      }
      struct Frame {StmtId id;std::optional<StmtId> child;bool entered=false;};
      std::vector<Frame> stack;budget.Array(stack,size);
      for(const auto root:flow.statement_order) {
        budget.Step();if(flow.statement_semantics[root.value]->parent)continue;
        std::size_t depth=1;stack[0]={root,children[root.value]};
        while(depth) {
          budget.Step();auto& frame=stack[depth-1];const auto exit=flow.statement_semantics[frame.id.value]->normal_exit;
          if(!frame.entered) {
            frame.entered=true;
            if(exit){previous[frame.id.value]=exits[exit->value];exits[exit->value]=frame.id;}
            for(auto at=queries[frame.id.value];at;at=jumps[*at].next) {
              budget.Step();++delta[frame.id.value];if(const auto consumer=exits[jumps[*at].target.value])--delta[consumer->value];
            }
          }
          if(frame.child){const auto child=*frame.child;frame.child=siblings[child.value];stack[depth++]={child,children[child.value]};continue;}
          if(exit)exits[exit->value]=previous[frame.id.value];--depth;
        }
      }
    }
    for(const auto id:flow.statement_order) {
      budget.Step();if(flow.statement_semantics[id.value]->completion==FlowStatementCompletion::kJump&&!queries[id.value])
        result.observations[id.value].abrupt=true; // Missing modeled route is not normal completion.
    }
    for(auto at=flow.statement_order.rbegin();at!=flow.statement_order.rend();++at) {
      budget.Step();const auto& statement=*flow.statement_semantics[at->value];if(!statement.parent)continue;
      const auto& child=result.observations[at->value];auto& parent=result.observations[statement.parent->value];
      if(child.evaluation==ExpressionEvaluation::kRequired)parent.evaluation=ExpressionEvaluation::kRequired;
      parent.definitions|=child.definitions;parent.abrupt|=child.abrupt;parent.cleanup|=child.cleanup;
      parent.repetition|=child.repetition;parent.suspension|=child.suspension;
      delta[statement.parent->value]+=delta[at->value];
    }
    for(const auto id:flow.statement_order){budget.Step();RequireTree(delta[id.value]>=0,TreeStructureError::kOwnership);result.observations[id.value].abrupt|=delta[id.value]>0;}
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  } catch(TreeStructureError error) {return error;
  } catch(const std::bad_alloc&) {return TreeStructureError::kResourceLimit;
  } catch(const std::length_error&) {return TreeStructureError::kResourceLimit;}
}
std::variant<FlowStatements,TreeStructureError> AnalyzeFlowStatements(
    const RecoveryFlow& flow,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);bool needed=false;
    for(const auto& statement:flow.statement_semantics){budget.Step();needed|=statement&&statement->repeat_point.has_value();}
    FlowCycles cycles;
    if(needed) {
      auto analyzed=AnalyzeFlowCycles(flow,budget.Remaining());if(const auto* error=std::get_if<TreeStructureError>(&analyzed))return *error;
      cycles=std::get<FlowCycles>(std::move(analyzed));budget.Admit(cycles.allocated_bytes,1);budget.Step(cycles.work);
    }
    auto analyzed=AnalyzeFlowStatements(flow,cycles,budget.Remaining());if(const auto* error=std::get_if<TreeStructureError>(&analyzed))return *error;
    auto result=std::get<FlowStatements>(std::move(analyzed));budget.Admit(result.allocated_bytes,1);budget.Step(result.work);
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;
  }catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;
  }catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
