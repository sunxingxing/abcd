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
  copy(result->exceptional_exits,graph.exceptional_exits);copy(result->phases,graph.phases);copy(result->origins,graph.origins);
  // Transfer the verified table; its allocation was already admitted with TreeFlow.
  result->definitions=std::move(flow.definitions);
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
      (original.definitions.empty()||original.definitions.size()==tree.expressions.size())&&
      (original.catches.empty()||original.catches.size()==tree.symbols.size())&&
      original.statements==tree.statements.size()&&original.bindings==tree.symbols.size(),TreeStructureError::kOwnership);
}
std::optional<OriginalResult> FindOriginalResult(const OriginalFlow& original,analysis::OpId id) {
  RequireTree(id.value<original.operations.size(),TreeStructureError::kOwnership);
  const auto& operation=original.operations[id.value];
  if(operation.life!=analysis::ValueLife::kLive||!operation.result||!operation.result_expression)return {};
  RequireTree(operation.result->value<original.values.size(),TreeStructureError::kOwnership);
  const auto& value=original.values[operation.result->value];
  if(value.life!=analysis::ValueLife::kLive)return {};
  return OriginalResult{*operation.result,*operation.result_expression,value.domain};
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
