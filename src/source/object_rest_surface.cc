#include "object_rest_surface.h"
#include "object_rest_plan.h"
#include "tree_flow.h"
#include "original_flow.h"
namespace abcd::source {
namespace {
constexpr auto kProof=TreeStructureError::kUnsupportedSyntax;
ObjectRestSurfaceField Field(const FunctionTree& tree,const OriginalFlow& original,analysis::OpId operation){
  const auto& op=original.operations[operation.value];RequireTree(op.expression&&op.result&&op.statement,kProof);
  const auto* s=std::get_if<ExpressionStmt>(&tree.statements[op.statement->value].statement);RequireTree(s,kProof);
  const auto* a=std::get_if<AssignmentExpr>(&tree.expressions[s->expression.value].expression);RequireTree(a&&a->value==*op.expression,kProof);
  const auto* target=std::get_if<SymbolTarget>(&a->target);RequireTree(target,kProof);
  const auto& d=original.definitions[s->expression.value];RequireTree(d&&d->binding==target->symbol&&d->input==a->value,kProof);const auto* r=std::get_if<OriginalResultDefinition>(&d->origin);RequireTree(r&&r->operation==operation&&r->value==*op.result,kProof);return {*op.statement,a->value,target->symbol,{},NameId{}};
}
}
void RetainObjectRests(const analysis::ValueProgram& graph,const EvaluationPlan& plan,const FunctionTree& tree,const TreeFlow&,OriginalFlow& original,TreeCheckBudget& b){
  auto certificate=BuildObjectRests(graph,plan,b);if(certificate.entries.empty())return;RequireTree(VerifyObjectRests(graph,plan,certificate,b),kProof);b.Array(original.object_rests,certificate.entries.size());
  for(std::size_t i=0;i<certificate.entries.size();++i){b.Step();const auto& proof=certificate.entries[i];const auto copy=Field(tree,original,proof.copy);auto& surface=original.object_rests[i];surface.statement=copy.statement;surface.expression=copy.expression;surface.target=copy.target;const auto* rest=std::get_if<ObjectRestExpr>(&tree.expressions[copy.expression.value].expression);RequireTree(rest,kProof);surface.source=rest->source;surface.keys=rest->keys;b.Array(surface.fields,proof.fields.size());for(std::size_t j=0;j<proof.fields.size();++j){b.Step();surface.fields[j]=Field(tree,original,proof.fields[j].read);const auto* member=std::get_if<MemberExpr>(&tree.expressions[surface.fields[j].expression.value].expression);RequireTree(member,kProof);surface.fields[j].base=member->base;surface.fields[j].key=member->key;}
  }
  VerifyObjectRestSurface(tree,original,b);
}
void VerifyObjectRestSurface(const FunctionTree& tree,const OriginalFlow& original,TreeCheckBudget& b){
  if(original.object_rests.empty())return;
  auto structure=tree.class_scope?VerifyClassInitializerStructure(tree,b.Remaining()):VerifyTreeStructure(tree,b.Remaining());
  if(const auto* error=std::get_if<TreeStructureError>(&structure))throw *error;
  const auto& scopes=std::get<TreeStructure>(structure);b.Admit(scopes.allocated_bytes,1);b.Step(scopes.work);
  std::size_t query_count=0;for(const auto& pattern:original.object_rests){b.Step();RequireTree(query_count<SIZE_MAX&&pattern.fields.size()<=SIZE_MAX-query_count-1,TreeStructureError::kResourceLimit);query_count+=pattern.fields.size()+1;}
  RequireTree(query_count<=SIZE_MAX/2,TreeStructureError::kResourceLimit);std::vector<TreeScopedReference> queries;b.Array(queries,2*query_count);std::size_t query=0;
  std::vector<std::uint8_t> consumed,captured;b.Array(consumed,tree.statements.size());b.Array(captured,tree.symbols.size());for(auto binding:tree.closure_captures){b.Step();captured[binding.value]=1;}
  const auto target=[&](SymbolId binding,ScopeId scope){RequireTree(binding.value<tree.symbols.size(),kProof);const auto& symbol=tree.symbols[binding.value];RequireTree(symbol.kind==BindingKind::kCompilerTemporary&&symbol.declaration==DeclarationKind::kLet&&symbol.scope==tree.body_scope&&!captured[binding.value],kProof);queries[query++]={binding,scope};};
  for(const auto& pattern:original.object_rests){b.Step();RequireTree(pattern.statement.value<tree.statements.size()&&!consumed[pattern.statement.value]++,kProof);const auto* statement=std::get_if<ExpressionStmt>(&tree.statements[pattern.statement.value].statement);RequireTree(statement,kProof);const auto* assignment=std::get_if<AssignmentExpr>(&tree.expressions[statement->expression.value].expression);const auto& definition=original.definitions[statement->expression.value];RequireTree(definition&&definition->binding==pattern.target&&definition->input==pattern.expression,kProof);RequireTree(assignment&&assignment->value==pattern.expression&&std::get_if<SymbolTarget>(&assignment->target)&&std::get<SymbolTarget>(assignment->target).symbol==pattern.target,kProof);const auto* rest=std::get_if<ObjectRestExpr>(&tree.expressions[pattern.expression.value].expression);const auto scope=scopes.expression_scopes[pattern.expression.value];target(pattern.target,scope);RequireTree(rest&&rest->source==pattern.source&&rest->keys.first==pattern.keys.first&&rest->keys.count==pattern.keys.count,kProof);
    std::size_t key_index=0;for(const auto& field:pattern.fields){b.Step();target(field.target,scope);RequireTree(field.statement.value<tree.statements.size()&&!consumed[field.statement.value]++,kProof);const auto* s=std::get_if<ExpressionStmt>(&tree.statements[field.statement.value].statement);RequireTree(s,kProof);const auto* a=std::get_if<AssignmentExpr>(&tree.expressions[s->expression.value].expression);const auto& defined=original.definitions[s->expression.value];RequireTree(defined&&defined->binding==field.target&&defined->input==field.expression,kProof);RequireTree(a&&a->value==field.expression&&std::get_if<SymbolTarget>(&a->target)&&std::get<SymbolTarget>(a->target).symbol==field.target,kProof);const auto* member=std::get_if<MemberExpr>(&tree.expressions[field.expression.value].expression);RequireTree(member&&member->base==field.base&&member->key==field.key,kProof);
      if(const auto* computed=std::get_if<ComputedKey>(&member->key)){const auto& operand=tree.expressions[computed->expression.value].expression;RequireTree(std::holds_alternative<SymbolExpr>(operand)||std::holds_alternative<TreeLiteral>(operand),kProof);RequireTree(key_index<rest->keys.count,kProof);const auto key=tree.arguments[rest->keys.first+key_index];if(const auto* read=std::get_if<SymbolExpr>(&tree.expressions[key.value].expression)){target(read->symbol,scope);RequireTree(!captured[read->symbol.value],kProof);}else RequireTree(std::holds_alternative<TreeLiteral>(tree.expressions[key.value].expression),kProof);}++key_index;
    }
  }
  queries.resize(query);auto resolved=ResolveTreeReferences(tree,queries,b);for(auto valid:resolved){b.Step();RequireTree(valid,kProof);}
}
}
