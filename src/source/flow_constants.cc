#include "flow_constants.h"
#include "primitive_value.h"
#include "recovery_scratch.h"
#include <limits>
#include <stdexcept>
namespace abcd::source {
FlowValueEquation FlowValueEquations::operator[](std::size_t i) const {
  RequireTree(i<base.size(),TreeStructureError::kOwnership);
  if(!replacements.empty()&&replacements[i])return FlowExpressionValue{*replacements[i]};
  const auto equation=base[i];
  if(const auto* read=std::get_if<FlowBindingValue>(&equation);read&&!bindings.empty()) {
    RequireTree(read->binding.value<bindings.size(),TreeStructureError::kOwnership);
    return bindings[read->binding.value];
  }
  return equation;
}
std::variant<FlowConstants,TreeStructureError> AnalyzeFlowConstants(
    const RecoveryFlow& flow,const FlowLiveness& live,std::span<const FlowDefinitionValue> values,analysis::SemanticBudget allowance) {
  return AnalyzeFlowConstants(flow,live,values,FlowValueEquations{},{},{},allowance);
}
std::variant<FlowConstants,TreeStructureError> AnalyzeFlowConstants(
    const RecoveryFlow& flow,const FlowLiveness& live,std::span<const FlowDefinitionValue> values,
    std::span<const FlowValueEquation> expressions,std::span<const TreeLiteral> primitives,
    std::span<const std::u16string> texts,analysis::SemanticBudget allowance) {
  return AnalyzeFlowConstants(flow,live,values,FlowValueEquations{expressions,{},{}},primitives,texts,allowance);
}
std::variant<FlowConstants,TreeStructureError> AnalyzeFlowConstants(
    const RecoveryFlow& flow,const FlowLiveness& live,std::span<const FlowDefinitionValue> values,
    FlowValueEquations expressions,std::span<const TreeLiteral> primitives,
    std::span<const std::u16string> texts,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);
    RequireTree(values.size()==flow.definitions.size()&&live.live_definitions.size()==values.size(),TreeStructureError::kOwnership);
    RequireTree(expressions.replacements.empty()||expressions.replacements.size()==expressions.size(),TreeStructureError::kOwnership);
    struct ValueId {std::uint32_t value;};
    struct DependencyId {std::uint32_t value;};
    struct Dependency {ValueId target;std::optional<DependencyId> next;};
    enum class ValueKind : std::uint8_t {kUnresolved,kPrimitive,kUnknown};
    // Terminal values need no mutable node, and binding reads use their binding
    // node directly. This contracts identities without discarding any operand.
    struct Value {ValueKind kind=ValueKind::kUnresolved;const TreeLiteral* primitive=nullptr;std::optional<PrimitiveId> literal;};
    const auto bindings=flow.bindings.size();
    RequireTree(expressions.size()<=std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
    const auto transfer=[](const FlowValueEquation& equation){return !std::holds_alternative<UnknownFlowValue>(equation)&&!std::holds_alternative<FlowPrimitiveReference>(equation)&&!std::holds_alternative<FlowBindingValue>(equation);};
    std::size_t active=0;
    for(std::size_t i=0;i<expressions.size();++i){budget.Step();active+=transfer(expressions[i]);}
    const auto size=bindings+active;
    RequireTree(size>=bindings&&size<=std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
    const auto binding=[&](SymbolId id){RequireTree(id.value<bindings,TreeStructureError::kOwnership);return ValueId{id.value};};
    std::vector<std::optional<ValueId>> expression_nodes;std::vector<ExprId> equations;
    budget.Array(expression_nodes,expressions.size());budget.Array(equations,active);
    std::size_t at=0;
    for(std::size_t i=0;i<expressions.size();++i) {
      budget.Step();const auto equation=expressions[i];
      if(const auto* literal=std::get_if<FlowPrimitiveReference>(&equation))
        RequireTree(literal->primitive.value<primitives.size(),TreeStructureError::kOwnership);
      if(const auto* read=std::get_if<FlowBindingValue>(&equation))expression_nodes[i]=binding(read->binding);
      if(!transfer(equation))continue;
      expression_nodes[i]=ValueId{static_cast<std::uint32_t>(bindings+at)};
      equations[at++]={static_cast<std::uint32_t>(i)};
    }
    const auto expression=[&](ExprId id){RequireTree(id.value<expressions.size(),TreeStructureError::kOwnership);return expression_nodes[id.value];};
    const auto retained=[&](std::size_t i){return flow.definitions[i].kind!=FlowDefinitionKind::kUndefined||live.live_definitions[i];};
    std::vector<Value> states;std::vector<std::optional<DependencyId>> heads;
    std::vector<std::uint8_t> queued;std::vector<ValueId> pending;std::vector<Dependency> dependencies;
    budget.Array(states,size);budget.Array(heads,size);budget.Array(queued,size);budget.Array(pending,size);
    const auto inputs=[&](const auto& equation,const auto& visit) {
      std::visit([&](const auto& value) {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,FlowBindingValue>)visit(std::optional{binding(value.binding)});
        else if constexpr(std::is_same_v<T,FlowExpressionValue>)visit(expression(value.expression));
        else if constexpr(std::is_same_v<T,FlowChoiceValue>){visit(expression(value.condition));visit(expression(value.consequent));visit(expression(value.alternate));}
        else if constexpr(std::is_same_v<T,FlowLogicalValue>){visit(expression(value.left));visit(expression(value.right));}
        else static_assert(std::is_same_v<T,UnknownFlowValue>||std::is_same_v<T,FlowPrimitiveReference>||std::is_same_v<T,FlowPrimitiveValue>);
      },equation);
    };
    std::size_t count=0;
    const auto count_input=[&](std::optional<ValueId> id){budget.Step();count+=id.has_value();};
    for(std::size_t i=0;i<values.size();++i){budget.Step();if(retained(i))inputs(values[i],count_input);}
    for(const auto id:equations){budget.Step();inputs(expressions[id.value],count_input);}
    RequireTree(count<=std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
    budget.Array(dependencies,count);std::size_t next=0,ready=0;
    const auto depend=[&](ValueId source,ValueId target){budget.Step();const DependencyId id{static_cast<std::uint32_t>(next++)};dependencies[id.value]={target,heads[source.value]};heads[source.value]=id;};
    const Value unknown{ValueKind::kUnknown,nullptr,{}};
    const auto read_expression=[&](ExprId id) {
      if(const auto source=expression(id))return states[source->value];
      const auto equation=expressions[id.value];
      if(const auto* literal=std::get_if<FlowPrimitiveReference>(&equation))
        return Value{ValueKind::kPrimitive,&primitives[literal->primitive.value],literal->primitive};
      return unknown;
    };
    const auto primitive=[](const FlowPrimitiveValue& value){return Value{ValueKind::kPrimitive,&value.value,value.literal};};
    const auto join=[&](Value a,const Value& b) {
      if(a.kind==ValueKind::kUnknown||b.kind==ValueKind::kUnresolved)return a;
      if(b.kind==ValueKind::kUnknown)return unknown;
      if(a.kind==ValueKind::kUnresolved)return b;
      if(!SamePrimitiveValue(*a.primitive,*b.primitive))return unknown;
      if(!a.literal)a.literal=b.literal;
      return a;
    };
    const auto update=[&](ValueId id,const Value& value) {
      budget.Step();auto& current=states[id.value];const auto merged=join(current,value);
      if(merged.kind==current.kind&&(merged.kind!=ValueKind::kPrimitive||merged.literal==current.literal))return;
      current=merged;if(!queued[id.value]){queued[id.value]=1;pending[ready++]=id;}
    };
    for(std::size_t i=0;i<values.size();++i) {
      budget.Step();if(!retained(i))continue;const auto target=binding(flow.definitions[i].binding);
      std::visit([&](const auto& value) {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,FlowBindingValue>)depend(binding(value.binding),target);
        else if constexpr(std::is_same_v<T,FlowExpressionValue>){if(const auto source=expression(value.expression))depend(*source,target);else update(target,read_expression(value.expression));}
        else if constexpr(std::is_same_v<T,FlowPrimitiveValue>)update(target,primitive(value));
        else {static_assert(std::is_same_v<T,UnknownFlowValue>);update(target,unknown);}
      },values[i]);
    }
    for(const auto id:equations) {
      budget.Step();const auto target=*expression(id);
      inputs(expressions[id.value],[&](std::optional<ValueId> source){if(source)depend(*source,target);});
    }
    for(std::size_t i=0;i<bindings;++i){budget.Step();if(flow.bindings[i].exposure!=FlowBindingExposure::kPrivate)update(binding({static_cast<std::uint32_t>(i)}),unknown);}
    for(const auto& fact:live.live_in){budget.Step();if(fact.point==flow.entry)update(binding(fact.binding),unknown);}
    const auto truth=[&](const TreeLiteral& value){if(const auto* text=std::get_if<TextId>(&value))RequireTree(text->value<texts.size(),TreeStructureError::kOwnership);return PrimitiveTruth(value,texts);};
    const auto evaluate=[&](ValueId id) {
      return std::visit([&](const auto& value)->Value {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,FlowBindingValue>)return states[binding(value.binding).value];
        else if constexpr(std::is_same_v<T,FlowExpressionValue>)return read_expression(value.expression);
        else if constexpr(std::is_same_v<T,FlowChoiceValue>) {
          const auto condition=read_expression(value.condition);
          const auto a=read_expression(value.consequent),b=read_expression(value.alternate);
          if(condition.kind==ValueKind::kPrimitive)return truth(*condition.primitive)?a:b;
          // Equal known arms ground the result independently of the predicate.
          if(condition.kind==ValueKind::kUnresolved) {
            if(a.kind==ValueKind::kPrimitive&&b.kind==ValueKind::kPrimitive&&SamePrimitiveValue(*a.primitive,*b.primitive))return join(a,b);
            return {};
          }
          return join(a,b);
        }else if constexpr(std::is_same_v<T,FlowLogicalValue>) {
          const auto left=read_expression(value.left);if(left.kind!=ValueKind::kPrimitive)return left;
          bool right=false;
          switch(value.operation) {
            case TreeLogical::kAnd:right=truth(*left.primitive);break;
            case TreeLogical::kOr:right=!truth(*left.primitive);break;
            case TreeLogical::kNullish:right=PrimitiveNullish(*left.primitive);break;
          }
          return right?read_expression(value.right):left;
        }else if constexpr(std::is_same_v<T,FlowPrimitiveReference>)return {ValueKind::kPrimitive,&primitives[value.primitive.value],value.primitive};
        else {static_assert(std::is_same_v<T,UnknownFlowValue>);return unknown;}
      },expressions[equations[id.value-bindings].value]);
    };
    // Initialize transfers with terminal operands that have no dependency node.
    // A selected known arm still supplies its value despite an unknown sibling.
    for(std::size_t i=bindings;i<size;++i){budget.Step();const ValueId id{static_cast<std::uint32_t>(i)};update(id,evaluate(id));}
    // Flat constant lattice (Wegman/Zadeck value propagation), without changing
    // executable edges. Each node advances bottom->primitive->unknown at most;
    // a printable identity adds at most one notification. Incremental joins
    // avoid rescanning all definitions when one dependency changes.
    const auto solve=[&] {
      while(ready) {
        budget.Step();const auto source=pending[--ready];queued[source.value]=0;
        for(auto edge=heads[source.value];edge;edge=dependencies[edge->value].next) {
          budget.Step();const auto target=dependencies[edge->value].target;
          update(target,target.value<bindings?states[source.value]:evaluate(target));
        }
      }
    };
    solve();
    // Missing sources poison even a provisionally grounded dependent. Unseeded
    // cycles cannot silently contribute an ignored bottom to a value join.
    for(std::size_t i=0;i<bindings;++i){budget.Step();if(states[i].kind==ValueKind::kUnresolved)update({static_cast<std::uint32_t>(i)},unknown);}
    solve();
    for(std::size_t i=bindings;i<size;++i){budget.Step();if(states[i].kind==ValueKind::kUnresolved)update({static_cast<std::uint32_t>(i)},unknown);}
    solve();
    FlowConstants result;budget.Array(result.values,bindings);
    for(std::size_t i=0;i<bindings;++i){budget.Step();if(states[i].kind==ValueKind::kPrimitive)result.values[i]=FlowPrimitiveValue{*states[i].primitive,states[i].literal};}
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
