#include "brace_plan.h"
namespace abcd::source {
namespace {
enum class Position : std::uint8_t {kRequired,kList,kAlternate};
bool Projected(StmtId id,const BindingInitializers& bindings,const NullishSyntax& nullish) {
  // Initializer relocation can create a printed lexical declaration even when
  // the original block scope has no symbols. Its new scope must remain intact.
  return bindings.statements[id.value] || bindings.omitted[id.value] || bindings.statement_values[id.value] ||
      bindings.declaration_completions[id.value] || bindings.declaration_values[id.value] ||
      (!nullish.statement_projections.empty() && nullish.statement_projections[id.value]);
}
}
BracePlan BuildBracePlan(const FunctionTree& tree,const BindingInitializers& bindings,
    const NullishSyntax& nullish,const LoopSyntax& loops,TreeCheckBudget& budget) {
  const auto start=budget.ActiveBytes();const auto work=budget.Work();BracePlan result;
  budget.Array(result.statements,tree.statements.size());
  {
    std::vector<std::uint8_t> bound,target;
    std::vector<Position> positions;
    budget.Array(bound,tree.scopes.size());budget.Array(target,tree.statements.size());
    budget.Array(positions,tree.statements.size());
    for(const auto& symbol:tree.symbols){budget.Step();bound.at(symbol.scope.value)=1;}
    if(tree.capture_scope)bound.at(tree.capture_scope->value)=1;
    for(std::size_t i=0;i<tree.statements.size();++i) {
      budget.Step();const auto& node=tree.statements[i].statement;
      if(const auto* block=std::get_if<BlockStmt>(&node)) {
        for(std::uint32_t j=0;j<block->child_count;++j) {
          budget.Step();positions.at(tree.children.at(block->first_child+j).value)=Position::kList;
        }
      } else if(const auto* branch=std::get_if<IfStmt>(&node)) {
        if(branch->alternate)positions.at(branch->alternate->value)=Position::kAlternate;
      }
      if(const auto* exit=std::get_if<BreakStmt>(&node))target.at(exit->target.value)=1;
      if(const auto* exit=std::get_if<ContinueStmt>(&node))target.at(exit->target.value)=1;
    }
    for(std::size_t i=0;i<tree.statements.size();++i) {
      budget.Step();auto& reason=result.statements[i];reason=BraceReason::kRequired;
      const auto* block=std::get_if<BlockStmt>(&tree.statements[i].statement);
      if(!block || i==tree.body.value || positions[i]==Position::kRequired)continue;
      if(block->label || target[i]){reason=BraceReason::kLabel;continue;}
      if(tree.scopes.at(block->scope.value).kind!=ScopeKind::kBlock){reason=BraceReason::kScope;continue;}
      if(bound.at(block->scope.value)){reason=BraceReason::kBinding;continue;}
      const auto& origin=tree.origins.at(tree.statements[i].origin.value);
      if(std::holds_alternative<LoopOrigin>(origin)||std::holds_alternative<TryOrigin>(origin)||
          std::holds_alternative<BindingOrigin>(origin)){reason=BraceReason::kScope;continue;}
      if(Projected({static_cast<std::uint32_t>(i)},bindings,nullish)||loops.tail_starts[i]){
        reason=BraceReason::kProjection;continue;
      }
      reason=BraceReason::kContent;
      if(!block->child_count)continue; // retain empty-block spans/completions conservatively
      bool clear=true;
      for(std::uint32_t j=0;j<block->child_count;++j){
        budget.Step();const auto child=tree.children.at(block->first_child+j);
        const auto& statement=tree.statements.at(child.value).statement;
        if(std::holds_alternative<DeclarationStmt>(statement)||std::holds_alternative<UnrecoveredStmt>(statement)||
            Projected(child,bindings,nullish)||loops.initializer_owners[child.value])clear=false;
      }
      if(!clear)continue;
      const auto first=tree.children[block->first_child];
      if(const auto* expression=std::get_if<ExpressionStmt>(&tree.statements[first.value].statement))
        if(const auto* literal=std::get_if<TreeLiteral>(&tree.expressions[expression->expression.value].expression))
          if(std::holds_alternative<TextId>(*literal)){reason=BraceReason::kDirective;continue;}
      // Empty-scope unlabelled BlockEvaluation returns exactly its StatementList
      // completion. Flattening preserves UpdateEmpty, abrupt payloads and order.
      if(positions[i]==Position::kList)reason=BraceReason::kStatementList;
      else if(block->child_count==1&&std::holds_alternative<IfStmt>(tree.statements[first.value].statement))
        reason=BraceReason::kElseIf; // inner arms stay braced: no dangling-else reassociation
    }
    result.allocated_bytes=budget.ActiveBytes()-start;
  } // Scratch is actually destroyed before the ledger gives credit.
  result.retained_bytes=result.statements.capacity()*sizeof(BraceReason);
  budget.Release(result.allocated_bytes-result.retained_bytes);
  result.work=budget.Work()-work;return result;
}
bool VerifyBracePlan(const FunctionTree& tree,const BindingInitializers& bindings,
    const NullishSyntax& nullish,const LoopSyntax& loops,const BracePlan& plan,TreeCheckBudget& budget) {
  return BuildBracePlan(tree,bindings,nullish,loops,budget).statements==plan.statements;
}
}
