#include "concurrent_tree.h"
#include "concurrent_completion.h"
#include "concurrent_async.h"
namespace abcd::source {
namespace {
using Error=TreeStructureError;
void Match(const FunctionTree& tree,const analysis::ValueProgram& graph) {
  RequireTree(tree.concurrent && tree.concurrent->Identity()==tree.identity&&tree.concurrent->Method()==tree.method &&
      tree.concurrent->Revision()==graph.revision&&tree.concurrent->CfgRevision()==graph.cfg_revision&&
      ConcurrentOutput(tree.identity.contract)==ConcurrentOutputPolicy::kArkDirectiveV1&&
      (tree.callable==CallableKind::kFunction||tree.callable==CallableKind::kAsyncFunction)&&
      (tree.callable==CallableKind::kAsyncFunction)==(tree.concurrent->Async()!=nullptr)&&tree.strictness==Strictness::kStrict&&
      tree.captures.empty()&&(!tree.self||tree.self_form!=SelfForm::kCreationBinding),Error::kOwnership);
}
ConcurrentCompletion Recompute(const analysis::ValueProgram& graph,TreeCheckBudget& budget) {
  auto built=BuildConcurrentCompletion(graph,budget.Remaining());
  if(const auto* error=std::get_if<ConcurrentFailure>(&built)) {
    if(error->kind==ConcurrentError::kResourceLimit)throw Error::kResourceLimit;
    if(error->kind==ConcurrentError::kWorkLimit)throw Error::kWorkLimit;
    throw Error::kOwnership;
  }
  auto proof=std::get<ConcurrentCompletion>(std::move(built));budget.Admit(proof.allocated_bytes,1);budget.Step(proof.work);return proof;
}
}
void VerifyConcurrentTree(const analysis::ValueProgram& graph,const EvaluationPlan& evaluation,
    const FunctionTree& tree,TreeFlow& flow,TreeCheckBudget& budget) {
  if(!tree.concurrent){RequireTree(!evaluation.concurrent,Error::kOwnership);return;}
  Match(tree,graph);
  if (tree.concurrent->Async()) {
    auto checked=VerifyConcurrentAsync(*tree.concurrent->Async(),graph,budget.Remaining());
    if(const auto* error=std::get_if<ConcurrentFailure>(&checked)) {
      if(error->kind==ConcurrentError::kResourceLimit)throw Error::kResourceLimit;
      if(error->kind==ConcurrentError::kWorkLimit)throw Error::kWorkLimit;
      throw Error::kOwnership;
    }
    budget.Step(std::get<std::uint64_t>(checked));
    RequireTree(!evaluation.concurrent,Error::kOwnership);return;
  }
  auto proof=Recompute(graph,budget);
  RequireTree(!evaluation.concurrent || (proof.returns==evaluation.concurrent->returns&&proof.members==evaluation.concurrent->members&&proof.uses==evaluation.concurrent->uses),Error::kOwnership);
  std::vector<std::optional<AnchorId>> anchors;budget.Array(anchors,graph.ops.size());
  for(const auto& placement:tree.operations) {
    budget.Step();if(placement.operation.value>=proof.members.size()||!proof.members[placement.operation.value])continue;
    const auto* anchor=std::get_if<AnchorId>(&placement.node);
    RequireTree(anchor&&!anchors[placement.operation.value],Error::kOwnership);anchors[placement.operation.value]=*anchor;
  }
  budget.Array(flow.notifications,proof.returns.size());
  for(std::size_t i=0;i<proof.returns.size();++i) {
    budget.Step();const auto& port=proof.returns[i];const auto anchor=anchors[port.notification.value];
    RequireTree(anchor&&anchor->value<tree.anchors.size()&&flow.statements[port.returned.value]&&
        tree.anchors[anchor->value].origin==TreeOriginId{graph.ops[port.notification.value].origin.value},Error::kOwnership);
    const auto statement=*flow.statements[port.returned.value];
    RequireTree(statement.value<tree.statements.size()&&std::holds_alternative<ReturnStmt>(tree.statements[statement.value].statement),Error::kOwnership);
    flow.notifications[i]={port.notification,port.returned,port.value,statement,*anchor};
  }
}
void VerifyOriginalConcurrent(const FunctionTree& tree,const OriginalFlow& original,TreeCheckBudget& budget) {
  RequireTree(tree.concurrent.has_value()==!original.concurrent_operations.empty(),Error::kOwnership);
  if(!tree.concurrent){RequireTree(original.notifications.empty(),Error::kOwnership);return;}
  RequireTree(original.concurrent_operations.size()==original.operations.size(),Error::kOwnership);
  analysis::ValueProgram graph;graph.revision=original.value_revision;graph.cfg_revision=original.cfg_revision;
  auto copy=[&](auto& into,const auto& from){budget.Array(into,from.size());std::copy(from.begin(),from.end(),into.begin());};
  copy(graph.values,original.values);copy(graph.uses,original.uses);copy(graph.blocks,original.blocks);
  copy(graph.edges,original.edges);copy(graph.exceptional_exits,original.exceptional_exits);copy(graph.origins,original.origins);copy(graph.phases,original.phases);
  budget.Array(graph.ops,original.operations.size());
  for(std::size_t i=0;i<graph.ops.size();++i) {
    budget.Step();const auto& op=original.operations[i];RequireTree(op.block.value<graph.blocks.size()&&i>=graph.blocks[op.block.value].first_op.value,Error::kOwnership);
    graph.ops[i]={original.concurrent_operations[i],op.block,static_cast<std::uint32_t>(i-graph.blocks[op.block.value].first_op.value),op.first_use,op.use_count,op.result,op.normal,op.before_throw,op.exceptional,op.origin,op.life};
  }
  Match(tree,graph);
  if(tree.concurrent->Async()) {
    auto checked=VerifyConcurrentAsync(*tree.concurrent->Async(),graph,budget.Remaining());
    if(const auto* error=std::get_if<ConcurrentFailure>(&checked)) {
      if(error->kind==ConcurrentError::kResourceLimit)throw Error::kResourceLimit;
      if(error->kind==ConcurrentError::kWorkLimit)throw Error::kWorkLimit;
      throw Error::kOwnership;
    }
    budget.Step(std::get<std::uint64_t>(checked)); RequireTree(original.notifications.empty(),Error::kOwnership);return;
  }
  auto proof=Recompute(graph,budget);
  RequireTree(proof.returns.size()==original.notifications.size(),Error::kOwnership);
  std::vector<std::optional<AnchorId>> anchors;budget.Array(anchors,graph.ops.size());
  for(const auto& placement:tree.operations) {
    budget.Step();if(placement.operation.value>=proof.members.size()||!proof.members[placement.operation.value])continue;
    const auto* anchor=std::get_if<AnchorId>(&placement.node);RequireTree(anchor&&!anchors[placement.operation.value],Error::kOwnership);anchors[placement.operation.value]=*anchor;
  }
  for(std::size_t i=0;i<proof.returns.size();++i) {
    budget.Step();const auto& expected=proof.returns[i];const auto& port=original.notifications[i];
    RequireTree(port.notification==expected.notification&&port.returned==expected.returned&&port.value==expected.value&&
        original.operations[port.returned.value].statement==port.statement&&anchors[port.notification.value]==port.anchor&&
        port.statement.value<tree.statements.size()&&std::holds_alternative<ReturnStmt>(tree.statements[port.statement.value].statement)&&
        port.anchor.value<tree.anchors.size()&&tree.anchors[port.anchor.value].origin==TreeOriginId{graph.ops[port.notification.value].origin.value},Error::kOwnership);
  }
}
}
