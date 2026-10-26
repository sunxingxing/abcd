#include "super_store.h"
#include "expression_children.h"
#include "expression_observation.h"
#include <limits>
namespace abcd::source {
std::vector<std::uint8_t> SuperStoreRhsSafety(
    const FunctionTree& tree, TreeCheckBudget& budget) {
  bool present = false;
  for (const auto& node : tree.expressions) {
    budget.Step();
    if (const auto* a = std::get_if<AssignmentExpr>(&node.expression))
      if (const auto* m = std::get_if<MemberTarget>(&a->target))
        present |= a->lookup == analysis::PropertyLookup::kSuper;
  }
  if (!present) return {};
  const auto n = tree.expressions.size();
  std::vector<std::size_t> offsets, cursor, pending, queue;
  std::vector<std::uint8_t> inert;
  RequireTree(n < std::numeric_limits<std::size_t>::max(), TreeStructureError::kResourceLimit);
  budget.Array(offsets,n+1); budget.Array(cursor,n);
  budget.Array(pending,n); budget.Array(queue,n); budget.Array(inert,n);
  for (std::size_t i=0; i<n; ++i) {
    budget.Step();
    VisitExpressionChildren(tree,tree.expressions[i].expression,[&](ExprId child) {
      budget.Step(); RequireTree(child.value<n,TreeStructureError::kRange);
      RequireTree(offsets[child.value+1]!=SIZE_MAX && pending[i]!=SIZE_MAX,TreeStructureError::kResourceLimit);
      ++offsets[child.value+1]; ++pending[i];
    });
  }
  for (std::size_t i=0; i<n; ++i) {
    budget.Step(); RequireTree(offsets[i+1]<=SIZE_MAX-offsets[i],TreeStructureError::kResourceLimit);
    offsets[i+1]+=offsets[i]; cursor[i]=offsets[i];
  }
  std::vector<std::size_t> parents; budget.Array(parents,offsets[n]);
  std::size_t head=0,tail=0;
  for(std::size_t i=0;i<n;++i) {
    budget.Step(); const auto& node=tree.expressions[i].expression;
    inert[i]=NormalValueEffects(node,{},kSourceResourceFailures).Evaluation()==ExpressionEvaluation::kInert;
    // Function body formals are initialized before body execution. Other
    // binding reads keep their guard unless common flow proves them separately.
    if(const auto* read=std::get_if<SymbolExpr>(&node))
      inert[i]=read->loading==analysis::ModuleLoading::kOrdinary && read->symbol.value<tree.symbols.size() &&
          tree.symbols[read->symbol.value].kind==BindingKind::kParameter;
    VisitExpressionChildren(tree,node,[&](ExprId child){budget.Step();parents[cursor[child.value]++]=i;});
    if(!pending[i])queue[tail++]=i;
  }
  while(head<tail) {
    budget.Step();const auto child=queue[head++];
    for(auto edge=offsets[child];edge<offsets[child+1];++edge) {
      budget.Step();const auto parent=parents[edge];inert[parent]&=inert[child];
      if(--pending[parent]==0)queue[tail++]=parent;
    }
  }
  RequireTree(tail==n,TreeStructureError::kOwnership);
  return inert;
}
}
