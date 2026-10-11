#include "original_flow.h"
#include "tree_flow.h"
#include "recovery_scratch.h"
#include <algorithm>
#include <limits>
#include <type_traits>
namespace abcd::source {
namespace {
analysis::ValueKinds Kinds(const OriginalFlow& original,const analysis::ValueOperand& operand) {
  return std::visit([&](const auto& value) {
    using T=std::decay_t<decltype(value)>;using K=analysis::ValueKinds;
    if constexpr(std::is_same_v<T,analysis::ValueId>) {
      RequireTree(value.value<original.values.size(),TreeStructureError::kOwnership);
      return original.values[value.value].domain;
    } else if constexpr(std::is_same_v<T,analysis::IntegerLiteral>||std::is_same_v<T,analysis::Float64Bits>)return K::Number();
    else {
      static_assert(std::is_same_v<T,analysis::SpecialValue>);
      switch(value) {
        case analysis::SpecialValue::kUndefined:return K::Language();
        case analysis::SpecialValue::kNull:return K::ZeroOrOnePrimitive();
        case analysis::SpecialValue::kTrue:case analysis::SpecialValue::kFalse:return K::Boolean();
        case analysis::SpecialValue::kNaN:case analysis::SpecialValue::kInfinity:return K::Number();
      }
    }
  },operand);
}
}
std::shared_ptr<OriginalFlow> RetainOriginalFlow(const analysis::ValueProgram& graph,
    const FunctionTree& tree,TreeFlow& flow,TreeCheckBudget& budget) {
  auto result=std::allocate_shared<OriginalFlow>(RecoveryAllocator<OriginalFlow>{budget});
  result->identity=tree.identity;result->method=tree.method;
  result->value_revision=graph.revision;result->cfg_revision=graph.cfg_revision;
  result->statements=tree.statements.size();result->bindings=tree.symbols.size();
  const auto copy=[&](auto& target,const auto& input) {
    budget.Array(target,input.size());std::copy(input.begin(),input.end(),target.begin());
  };
  copy(result->values,graph.values);copy(result->uses,graph.uses);
  copy(result->blocks,graph.blocks);copy(result->edges,graph.edges);
  result->edge_controls=std::move(flow.edge_controls);
  copy(result->exceptional_exits,graph.exceptional_exits);copy(result->phases,graph.phases);copy(result->origins,graph.origins);
  bool causes=false;
  for(const auto& exit:graph.exceptional_exits) {
    budget.Step();RequireTree(exit.op.value<graph.ops.size(),TreeStructureError::kOwnership);
    if(graph.ops[exit.op.value].exception_cause!=analysis::ExceptionCause::kUnclassified){causes=true;break;}
  }
  if(causes) {
    budget.Array(result->exception_causes,graph.exceptional_exits.size());
    for(std::size_t i=0;i<graph.exceptional_exits.size();++i) {
      budget.Step();const auto op=graph.exceptional_exits[i].op;
      RequireTree(op.value<graph.ops.size(),TreeStructureError::kOwnership);
      result->exception_causes[i]=graph.ops[op.value].exception_cause;
    }
  }
  // Transfer the verified table; its allocation was already admitted with TreeFlow.
  result->definitions=std::move(flow.definitions);
  result->declarations=std::move(flow.declarations);
  if(!flow.cell_bindings.empty()) {
    result->cells.emplace();result->cells->bindings=std::move(flow.cell_bindings);
    budget.Array(result->cells->references,graph.ops.size());
  }
  if(!flow.catch_parameters.empty()) {
    RequireTree(flow.catch_parameters.size()==graph.values.size(),TreeStructureError::kOwnership);
    budget.Array(result->catches,tree.symbols.size());
    std::vector<std::optional<StmtId>> bodies;budget.Array(bodies,tree.symbols.size());
    for(const auto statement:flow.tries) {
      budget.Step();RequireTree(statement.has_value(),TreeStructureError::kOwnership);
      const auto& clause=std::get<TryStmt>(tree.statements[statement->value].statement).handler;
      RequireTree(clause&&clause->parameter.value<bodies.size()&&!bodies[clause->parameter.value],TreeStructureError::kOwnership);
      bodies[clause->parameter.value]=clause->body;
    }
    for(std::size_t i=0;i<flow.catch_parameters.size();++i) {
      budget.Step();const auto binding=flow.catch_parameters[i];if(!binding)continue;
      const auto* caught=std::get_if<analysis::CatchDefinition>(&graph.values[i].origin);
      RequireTree(caught&&binding->value<bodies.size()&&bodies[binding->value]&&!result->catches[binding->value],TreeStructureError::kOwnership);
      result->catches[binding->value]=OriginalCatchBinding{analysis::ValueId{static_cast<std::uint32_t>(i)},caught->block,*bodies[binding->value]};
    }
  }
  budget.Array(result->operations,graph.ops.size());
  budget.Array(result->input_ranges,tree.expressions.size());
  RequireTree(flow.expressions.size()==graph.ops.size()&&flow.statements.size()==graph.ops.size(),TreeStructureError::kOwnership);
  for(std::size_t i=0;i<graph.ops.size();++i) {
    budget.Step();const auto& op=graph.ops[i];
    result->operations[i]={op.block,op.first_use,op.use_count,op.result,op.normal,
        op.before_throw,op.exceptional,op.life,op.origin,flow.expressions[i],flow.statements[i]};
    if(op.life==analysis::ValueLife::kLive&&flow.statements[i]) {
      if(std::holds_alternative<analysis::ValueThrow>(op.operation))result->operations[i].statement_kind=OriginalStatementKind::kThrow;
      else if(std::holds_alternative<analysis::ValueReturn>(op.operation))result->operations[i].statement_kind=OriginalStatementKind::kReturn;
      else if(std::holds_alternative<analysis::ValueDebugger>(op.operation))result->operations[i].statement_kind=OriginalStatementKind::kNormal;
    }
  }
  return result;
}
void RecordOriginalInput(OriginalFlow& original,const analysis::ValueOperand& operand,
    const analysis::ValueOperand& resolved,ExprId expression,
    std::optional<analysis::UseId> occurrence,TreeCheckBudget& budget) {
  budget.Step();
  RequireTree(expression.value<original.input_ranges.size(),TreeStructureError::kOwnership);
  if(const auto* value=std::get_if<analysis::ValueId>(&resolved))
    RequireTree(value->value<original.values.size(),TreeStructureError::kOwnership);
  if(original.inputs.size()==original.inputs.capacity()) {
    const auto capacity=original.inputs.capacity();
    RequireTree(capacity<=std::numeric_limits<std::size_t>::max()/2,TreeStructureError::kResourceLimit);
    const auto next=capacity?capacity*2:1;
    budget.Admit(next-capacity,sizeof(OriginalInput));original.inputs.reserve(next);
    budget.Admit(original.inputs.capacity()-next,sizeof(OriginalInput));
  }
  original.inputs.push_back({expression,operand,resolved,occurrence});
}
void IndexOriginalInputs(OriginalFlow& original,TreeCheckBudget& budget) {
  // One counting scatter, O(expressions + checked operand occurrences), rather
  // than sorting or looking up every source read independently.
  for(const auto& input:original.inputs) {budget.Step();++original.input_ranges[input.expression.value].count;}
  std::size_t next=0;
  for(auto& range:original.input_ranges) {budget.Step();range.first=next;next+=range.count;range.count=0;}
  std::vector<OriginalInput> sorted;budget.Array(sorted,original.inputs.size());
  for(const auto& input:original.inputs) {
    budget.Step();auto& range=original.input_ranges[input.expression.value];sorted[range.first+range.count++]=input;
  }
  original.inputs=std::move(sorted);
}
void CheckOriginalFlow(const FunctionTree& tree,const OriginalFlow& original) {
  RequireTree(original.identity==tree.identity&&original.method==tree.method&&
      original.value_revision==tree.value_revision&&original.cfg_revision==tree.cfg_revision&&
      original.input_ranges.size()==tree.expressions.size()&&
      (original.action_region_members.empty()||original.action_region_members.size()==original.operations.size())&&
      (original.definitions.empty()||original.definitions.size()==tree.expressions.size())&&
      (original.declarations.empty()||original.declarations.size()==tree.statements.size())&&
      (original.catches.empty()||original.catches.size()==tree.symbols.size())&&
      (original.exception_causes.empty()||original.exception_causes.size()==original.exceptional_exits.size())&&
      (!original.cells||original.cells->references.size()==original.operations.size())&&
      (!original.environments||(original.environments->revision==original.value_revision&&
        original.environments->cfg_revision==original.cfg_revision&&
        original.environments->flow.before.size()==original.operations.size()&&
        original.environments->normal_after.size()==original.operations.size()))&&
      (original.edge_controls.empty()||original.edge_controls.size()==original.edges.size())&&
      original.statements==tree.statements.size()&&original.bindings==tree.symbols.size(),TreeStructureError::kOwnership);
}
const OriginalActionRegion* FindOriginalActionRegion(const OriginalFlow& original,analysis::OpId operation) {
  if(operation.value>=original.action_region_members.size())return nullptr;
  const auto id=original.action_region_members[operation.value];
  if(!id||id->value>=original.action_regions.size())return nullptr;
  const auto& region=original.action_regions[id->value];
  for(const auto endpoint:{region.entry,region.exit,region.result})
    if(endpoint.value>=original.action_region_members.size()||
        original.action_region_members[endpoint.value]!=id||
        original.operations[endpoint.value].life!=analysis::ValueLife::kLive)return nullptr;
  if(!original.phases.empty()) {
    if(!region.commit||region.commit->value>=original.phases.size())return nullptr;
    const auto& completion=original.phases[region.commit->value];
    if(completion.operation!=region.exit||completion.phase!=analysis::ExceptionPhase::kCommit)return nullptr;
  }
  return &region;
}
std::optional<OriginalResult> FindOriginalResult(const OriginalFlow& original,analysis::OpId id) {
  RequireTree(id.value<original.operations.size(),TreeStructureError::kOwnership);
  const auto& operation=original.operations[id.value];
  if(operation.life!=analysis::ValueLife::kLive||!operation.result)return {};
  const auto* region=FindOriginalActionRegion(original,id);
  const auto expression=region&&region->result==id?std::optional{region->expression}:operation.result_expression;
  if(!expression)return {};
  RequireTree(operation.result->value<original.values.size(),TreeStructureError::kOwnership);
  const auto& value=original.values[operation.result->value];
  if(value.life!=analysis::ValueLife::kLive)return {};
  const auto* result=std::get_if<analysis::OpResult>(&value.origin);
  if(region&&!original.phases.empty()) {
    if(!region->commit)return {};
    return OriginalResult{*operation.result,*expression,value.domain,region->commit,region->exit};
  }
  return OriginalResult{*operation.result,*expression,value.domain,result?result->commit:std::nullopt,id};
}
analysis::ValueKinds OriginalDefinitionKinds(const OriginalFlow& original,const OriginalSourceDefinition& definition) {
  return std::visit([&](const auto& origin) {
    using T=std::decay_t<decltype(origin)>;
    if constexpr(std::is_same_v<T,OriginalSavedDefinition>)return Kinds(original,origin.operand);
    else if constexpr(std::is_same_v<T,OriginalResultDefinition>) {
      const auto result=FindOriginalResult(original,origin.operation);
      if(!result||result->value!=origin.value||result->expression!=definition.input)return analysis::ValueKinds{};
      return result->kinds;
    } else {
      static_assert(std::is_same_v<T,OriginalPhiDefinition>||std::is_same_v<T,OriginalCatchDefinition>);
      return Kinds(original,analysis::ValueOperand{origin.value});
    }
  },definition.origin);
}
}  // namespace abcd::source
