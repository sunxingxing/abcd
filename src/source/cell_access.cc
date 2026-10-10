#include "cell_access.h"
#include "class_tree.h"
#include <limits>
namespace abcd::source {
CellVisibility JoinCellVisibility(CellVisibility a,CellVisibility b) {
  switch(a) {
    case CellVisibility::kExternal:return CellVisibility::kExternal;
    case CellVisibility::kClosed:return b;
    case CellVisibility::kUnmodeled:
      switch(b) {
        case CellVisibility::kExternal:return CellVisibility::kExternal;
        case CellVisibility::kClosed:case CellVisibility::kUnmodeled:return CellVisibility::kUnmodeled;
      }
  }
  throw TreeStructureError::kOwnership;
}
std::span<const CellCapture> SourceCells::Captured(ExprId creation,TreeCheckBudget& budget) const {
  const auto first=std::lower_bound(captures.begin(),captures.end(),creation,
      [&](const auto& at,ExprId key){budget.Step();return at.creation<key;});
  const auto last=std::upper_bound(first,captures.end(),creation,
      [&](ExprId key,const auto& at){budget.Step();return key<at.creation;});
  return std::span<const CellCapture>(captures).subspan(first-captures.begin(),last-first);
}
SourceCells AnalyzeSourceCells(const FunctionTree& tree,TreeCheckBudget& budget) {
  SourceCells result;result.owner=&tree;bool complete=tree.recovery_issues.empty();
  bool simple=true;
  for(const auto& parameter:tree.parameters){budget.Step();simple&=!parameter.initializer&&!parameter.rest;}
  result.mapped_arguments=tree.strictness==Strictness::kSloppy&&!IsArrowCallable(tree.callable)&&simple;
  budget.Array(result.access,tree.symbols.size());
  for(std::size_t i=0;i<tree.symbols.size();++i){
    budget.Step();const auto& symbol=tree.symbols[i];
    RequireTree(symbol.scope.value<tree.scopes.size(),TreeStructureError::kScope);
    switch(symbol.kind){
      case BindingKind::kParameter:result.access[i]=result.mapped_arguments?CellVisibility::kExternal:CellVisibility::kClosed;break;
      case BindingKind::kCapture:case BindingKind::kModule:result.access[i]=CellVisibility::kExternal;break;
      case BindingKind::kLocal:case BindingKind::kCompilerTemporary:
      case BindingKind::kFunctionSelf:case BindingKind::kCatchParameter:
        result.access[i]=tree.scopes[symbol.scope.value].kind==ScopeKind::kModule?CellVisibility::kExternal:CellVisibility::kClosed;break;
    }
  }
  const auto exposed=[&](SymbolId symbol){
    RequireTree(symbol.value<result.access.size(),TreeStructureError::kBinding);
    result.access[symbol.value]=CellVisibility::kExternal;
  };
  for(const auto symbol:tree.closure_captures){budget.Step();exposed(symbol);}
  for(const auto& alias:tree.modules){budget.Step();exposed(alias.symbol);}
  // Count/fill the same typed edges. Missing class bodies/control supply an
  // explicit capability gap; do not infer closure access from names or layout.
  const auto visit=[&](const auto& capture){
    const auto family=[&](const std::optional<PreparedFamily>& prepared,ExprId creation){
      budget.Step();if(!prepared)return;
      const auto& members=prepared->Family();
      RequireTree(members.root.value<members.members.size(),TreeStructureError::kOwnership);
      const auto& child=members.members[members.root.value].tree;
      complete&=child.recovery_issues.empty();
      for(const auto& alias:child.captures){budget.Step();if(alias.target.owner==tree.owner)capture(creation,alias.target.symbol);}
    };
    for(std::size_t i=0;i<tree.expressions.size();++i){
      budget.Step();const ExprId id{static_cast<std::uint32_t>(i)};
      const auto& node=tree.expressions[i].expression;
      if(const auto* created=std::get_if<FunctionExpr>(&node)){
        RequireTree(created->captures.first<=tree.closure_captures.size()&&
            created->captures.count<=tree.closure_captures.size()-created->captures.first,TreeStructureError::kOwnership);
        for(const auto symbol:std::span<const SymbolId>(tree.closure_captures).subspan(created->captures.first,created->captures.count)){
          budget.Step();capture(id,symbol);}
      }
      if(const auto* created=std::get_if<ClassExpr>(&node)){
        RequireTree(bool(created->definition),TreeStructureError::kOwnership);
        const auto& definition=*created->definition;
        complete&=definition.BodiesPrepared();family(definition.constructor,id);
        for(const auto& member:definition.members)family(member,id);
        for(const auto& site:definition.definitions)for(const auto& body:site.bodies)family(body,id);
        if(definition.initializer){
          complete&=definition.initializer->tree.recovery_issues.empty();
          for(const auto& alias:definition.initializer->tree.captures){budget.Step();if(alias.target.owner==tree.owner)capture(id,alias.target.symbol);}
        }
      }
    }
  };
  RequireTree(tree.expressions.size()<=std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
  std::size_t count=0;
  visit([&](ExprId,SymbolId){RequireTree(count<std::numeric_limits<std::size_t>::max(),TreeStructureError::kResourceLimit);++count;});
  budget.Array(result.captures,count);std::size_t next=0;
  visit([&](ExprId site,SymbolId symbol){exposed(symbol);result.captures[next++]={site,symbol};});
  std::sort(result.captures.begin(),result.captures.end(),
      [&](const auto& a,const auto& b){budget.Step();return a.creation<b.creation;});
  if(!complete)for(auto& access:result.access){budget.Step();if(access==CellVisibility::kClosed)access=CellVisibility::kUnmodeled;}
  return result;
}
}  // namespace abcd::source
