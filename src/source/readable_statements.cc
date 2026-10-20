#include "readable_statements.h"
#include "discarded_read.h"
#include "original_flow.h"
#include "statement_children.h"
#include "grouping_plan.h"
#include "switch_syntax.h"
#include "cell_access.h"

namespace abcd::source {
namespace {
bool Projected(StmtId id, const BindingInitializers& b, const NullishSyntax& n,
               const LoopSyntax& loops) {
  return b.omitted[id.value] || b.statements[id.value] || b.statement_values[id.value] ||
      b.declaration_values[id.value] || b.declaration_completions[id.value] ||
      loops.initializer_owners[id.value] || loops.tail_starts[id.value] ||
      (!n.statement_projections.empty() && n.statement_projections[id.value]);
}
}
ReadableStatements BuildReadableStatements(const FunctionTree& tree,
    const BindingInitializers& bindings, const NullishSyntax& nullish,
    const LoopSyntax& loops, const OriginalFlow* original, TreeCheckBudget& budget) {
  switch(kReadablePrinterPolicy){case PrinterPolicy::kReadableV1:break;}
  ReadableStatements out;
  const auto count = tree.statements.size();
  budget.Array(out.guards,count);budget.Array(out.branches,count);
  budget.Array(out.plain_breaks,count);budget.Array(out.omit_labels,count);
  budget.Array(out.omit_snapshots,count);budget.Array(out.switch_values,count);
  std::vector<std::uint32_t> reads,writes;
  std::vector<std::uint8_t> captured;
  budget.Array(reads,tree.symbols.size());budget.Array(writes,tree.symbols.size());
  budget.Array(captured,tree.symbols.size());
  for(const auto symbol:tree.closure_captures){budget.Step();captured.at(symbol.value)=1;}
  for(const auto& e:tree.expressions){budget.Step();
    if(const auto* ref=std::get_if<SymbolExpr>(&e.expression))++reads.at(ref->symbol.value);
    if(const auto* assignment=std::get_if<AssignmentExpr>(&e.expression))
      if(const auto* target=std::get_if<SymbolTarget>(&assignment->target))++writes.at(target->symbol.value);
    if(const auto* update=std::get_if<UpdateExpr>(&e.expression))++writes.at(update->symbol.value);
  }
  bool switches=false;
  for(const auto& s:tree.statements){budget.Step();
    switches|=std::holds_alternative<SwitchStmt>(s.statement);
    if(const auto* d=std::get_if<DeclarationStmt>(&s.statement);d&&d->initializer)++writes.at(d->symbol.value);
  }
  std::optional<SourceCells> cells;
  if(switches)cells=AnalyzeSourceCells(tree,budget);
  std::vector<std::uint8_t> bound, guarded, empty;
  std::vector<std::uint32_t> label_uses, removed_uses;
  budget.Array(bound,tree.scopes.size());budget.Array(guarded,tree.expressions.size());
  budget.Array(empty,count);budget.Array(label_uses,tree.labels.size());
  budget.Array(removed_uses,tree.labels.size());
  for(const auto& s:tree.symbols){budget.Step();bound.at(s.scope.value)=1;}
  if(tree.capture_scope)bound.at(tree.capture_scope->value)=1;
  // OriginalActionRegion is the independently checked raw read+HoleGuard
  // composition. A coincident identifier spelling is never guard evidence.
  if(original)for(const auto& r:original->action_regions){
    budget.Step();
    if(r.expression.value<guarded.size() && r.entry!=r.exit &&
        r.reference.value<tree.symbols.size() && FindOriginalActionRegion(*original,r.entry)==&r) {
      const auto* ref=std::get_if<SymbolExpr>(&tree.expressions[r.expression.value].expression);
      if(ref&&ref->symbol==r.reference)guarded[r.expression.value]=1;
    }
  }
  if(original)for(const auto& operation:original->operations) {
    budget.Step();if(!operation.initialized_guard)continue;
    const auto& proof=*operation.initialized_guard;
    const auto result=FindOriginalResult(*original,proof.load);
    if(!result || result->value!=proof.value || result->expression.value>=guarded.size())continue;
    // An initialized-cell guard is a verified non-Hole fact, not an absent
    // certificate. Retain the raw read/guard identity even when the VM guard
    // has already become a nonexecuting source anchor.
    const auto* read=std::get_if<SymbolExpr>(&tree.expressions[result->expression.value].expression);
    if(read && original->cells && proof.load.value<original->cells->references.size()) {
      const auto& ref=original->cells->references[proof.load.value];
      if(ref && ref->kind==CellAccessKind::kLoad && ref->expression==result->expression &&
          ref->cell.value<original->cells->bindings.size() &&
          original->cells->bindings[ref->cell.value].binding==read->symbol)guarded[result->expression.value]=1;
    }
  }
  for(std::size_t i=0;i<count;++i){
    budget.Step();const auto id=StmtId{static_cast<std::uint32_t>(i)};
    const auto* b=std::get_if<BlockStmt>(&tree.statements[i].statement);
    if(b&&!b->label&&!bound[b->scope.value] &&
        tree.scopes[b->scope.value].kind==ScopeKind::kBlock && !Projected(id,bindings,nullish,loops)) {
      bool clear=true;
      for(std::uint32_t j=0;j<b->child_count;++j){budget.Step();
        const auto child=tree.children[b->first_child+j];
        clear &= bindings.omitted[child.value] || DiscardedInvocationRead(tree,child).has_value();
      }
      empty[i]=clear;
    }
    const auto* block=std::get_if<BlockStmt>(&tree.statements[i].statement);
    if(!block)continue;
    for(std::uint32_t j=0;j<block->child_count;++j){
      budget.Step();const auto child=tree.children[block->first_child+j];
      // A switch's repeated cache reads print once. Forward only its sole
      // adjacent private definition; captured/observable/name-inferred values
      // and every additional read/write keep the original snapshot.
      if(j && std::holds_alternative<SwitchStmt>(tree.statements[child.value].statement)) {
        const auto& sw=std::get<SwitchStmt>(tree.statements[child.value].statement);
        const auto syntax=VerifySwitchSyntax(tree,sw,budget);
        const auto previous=tree.children[block->first_child+j-1];
        std::optional<ExprId> value;std::optional<SymbolId> target;
        if(const auto* d=std::get_if<DeclarationStmt>(&tree.statements[previous.value].statement)){
          value=d->initializer;target=d->symbol;
        } else if(const auto* st=std::get_if<ExpressionStmt>(&tree.statements[previous.value].statement))
          if(const auto* a=std::get_if<AssignmentExpr>(&tree.expressions[st->expression.value].expression))
            if(const auto* t=std::get_if<SymbolTarget>(&a->target)){value=a->value;target=t->symbol;}
        const auto& cases=std::get<BlockStmt>(tree.statements[sw.body.value].statement);
        std::uint32_t uses=0;
        for(std::uint32_t c=0;c<cases.child_count;++c){budget.Step();
          const auto& clause=std::get<SwitchCaseStmt>(tree.statements[tree.children[cases.first_child+c].value].statement);
          if(clause.condition)++uses;
        }
        if(value && target==syntax.cache && cells && cells->captures_complete &&
            cells->access[syntax.cache.value]==CellVisibility::kClosed && !captured[syntax.cache.value] &&
            tree.symbols[syntax.cache.value].kind==BindingKind::kCompilerTemporary &&
            bindings.Storage(syntax.cache)==syntax.cache && bindings.ReadBinding(syntax.cache)==syntax.cache &&
            reads[syntax.cache.value]==uses && writes[syntax.cache.value]==1 &&
            !Projected(child,bindings,nullish,loops) && !bindings.omitted[previous.value] &&
            !bindings.statement_values[previous.value] && !bindings.declaration_values[previous.value] &&
            !bindings.declaration_completions[previous.value] && !loops.initializer_owners[previous.value] &&
            !loops.tail_starts[previous.value] &&
            !std::holds_alternative<FunctionExpr>(tree.expressions[value->value].expression) &&
            !std::holds_alternative<ClassExpr>(tree.expressions[value->value].expression)) {
          out.switch_values[child.value]=value;out.omit_snapshots[previous.value]=1;
        }
      }
      const auto* expr=std::get_if<ExpressionStmt>(&tree.statements[child.value].statement);
      if(!expr)continue;
      const auto* read=std::get_if<SymbolExpr>(&tree.expressions[expr->expression.value].expression);
      if(!read)continue;
      out.guards[child.value]=ReadableGuard::kNoCertificate;
      // A lazy module GetValue can execute dependency code, not just a TDZ
      // check. A native write cannot replace that independent observation.
      if(read->loading!=analysis::ModuleLoading::kOrdinary)continue;
      if(j+1==block->child_count)continue;
      const auto next=tree.children[block->first_child+j+1];
      const auto* statement=std::get_if<ExpressionStmt>(&tree.statements[next.value].statement);
      if(!statement)continue;
      const auto* assignment=std::get_if<AssignmentExpr>(&tree.expressions[statement->expression.value].expression);
      const auto* update=std::get_if<UpdateExpr>(&tree.expressions[statement->expression.value].expression);
      std::optional<SymbolId> target;
      if(assignment) {
        if(const auto* symbol=std::get_if<SymbolTarget>(&assignment->target))target=symbol->symbol;
      } else if(update)target=update->symbol;
      if(!target)continue;
      out.guards[child.value]=ReadableGuard::kBinding;
      // Compare actual projected storage/reference identity, never just names.
      if(bindings.ReadBinding(expr->expression,read->symbol)!=bindings.Storage(*target))continue;
      out.guards[child.value]=ReadableGuard::kProjection;
      if(Projected(child,bindings,nullish,loops)||Projected(next,bindings,nullish,loops)||
          bindings.replacements[expr->expression.value] ||
          bindings.replacements[statement->expression.value] ||
          (!bindings.read_literals.empty() && bindings.read_literals[expr->expression.value]) ||
          (!bindings.literals.empty() && bindings.literals[read->symbol.value]))continue;
      out.guards[child.value]=ReadableGuard::kNoCertificate;
      if(!guarded[expr->expression.value])continue;
      out.guards[child.value]=ReadableGuard::kRhsEffect;
      // Whole projected RHS summary includes every child, hook, throw and
      // suspension. PutValue's TDZ test moves only across an inert evaluation.
      if(assignment && bindings.evaluations[assignment->value.value]!=ExpressionEvaluation::kInert)continue;
      // Native update GetValue is its first action, before ToNumeric and
      // PutValue. The same binding's TDZ check therefore does not move at all.
      out.guards[child.value]=ReadableGuard::kRemove;
    }
  }
  struct Visit {StmtId id;std::optional<StmtId> breakable;};
  std::vector<Visit> stack;budget.Array(stack,count);std::size_t size=0;
  if(count)stack[size++]={tree.body,{}};
  while(size){
    budget.Step();const auto at=stack[--size];const auto& node=tree.statements[at.id.value].statement;
    if(const auto* b=std::get_if<IfStmt>(&node);b&&b->alternate&&!Projected(at.id,bindings,nullish,loops)){
      if(empty[b->alternate->value])out.branches[at.id.value]=ReadableBranch::kDropElse;
      else if(empty[b->consequent.value])out.branches[at.id.value]=ReadableBranch::kInvert;
    }
    if(const auto* exit=std::get_if<BreakStmt>(&node);exit&&exit->label){
      ++label_uses.at(exit->label->value);
      if(at.breakable==exit->target && std::holds_alternative<SwitchStmt>(tree.statements[exit->target.value].statement)){
        out.plain_breaks[at.id.value]=1;++removed_uses.at(exit->label->value);
      }
    }
    if(const auto* exit=std::get_if<ContinueStmt>(&node);exit&&exit->label)++label_uses.at(exit->label->value);
    auto owner=at.breakable;
    if(std::holds_alternative<SwitchStmt>(node)||std::holds_alternative<LoopStmt>(node)||
        std::holds_alternative<ForInStmt>(node)||std::holds_alternative<ForOfStmt>(node))owner=at.id;
    StatementChildren(tree,at.id,[&](StmtId child){budget.Step();RequireTree(size<stack.size(),TreeStructureError::kOwnership);stack[size++]={child,owner};});
  }
  for(std::size_t i=0;i<count;++i){budget.Step();
    if(const auto* sw=std::get_if<SwitchStmt>(&tree.statements[i].statement);sw&&sw->label)
      out.omit_labels[i]=label_uses[sw->label->value]==removed_uses[sw->label->value];
  }
  for(std::size_t i=0;i<count;++i){budget.Step();
    if(const auto* b=std::get_if<BreakStmt>(&tree.statements[i].statement);b&&b->label&&out.plain_breaks[i])
      out.plain_breaks[i]=label_uses[b->label->value]==removed_uses[b->label->value];
  }
  return out;
}
bool VerifyReadableStatements(const FunctionTree& t,const BindingInitializers& b,
    const NullishSyntax& n,const LoopSyntax& l,const OriginalFlow* original,
    const ReadableStatements& plan,TreeCheckBudget& budget){
  return plan==BuildReadableStatements(t,b,n,l,original,budget);
}
}
