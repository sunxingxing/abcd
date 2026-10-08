#include "flow_constants.h"
#include "primitive_value.h"
#include "recovery_scratch.h"
#include <limits>
#include <algorithm>
#include <stdexcept>
namespace abcd::source {
namespace {
std::variant<FlowConstants,TreeStructureError> SolveConstants(
    const RecoveryFlow&,const FlowLiveness&,std::span<const FlowDefinitionValue>,
    FlowValueEquations,std::span<const TreeLiteral>,std::span<const std::u16string>,analysis::SemanticBudget);
}
FlowValueEquation FlowValueEquations::operator[](std::size_t i) const {
  RequireTree(i<base.size(),TreeStructureError::kOwnership);
  if(!transports.empty()&&transports[i])return FlowPrimitiveReference{transports[i]->replacement};
  if(!replacements.empty()&&replacements[i])return FlowExpressionValue{*replacements[i]};
  if(!read_values.empty()&&read_values[i])return FlowPrimitiveReference{*read_values[i]};
  auto equation=base[i];
  if(const auto* read=std::get_if<FlowBindingValue>(&equation);read&&!bindings.empty()) {
    RequireTree(read->binding.value<bindings.size(),TreeStructureError::kOwnership);
    equation=bindings[read->binding.value];
  }
  if(const auto* read=std::get_if<FlowBindingValue>(&equation);read&&!invariants.empty()) {
    RequireTree(read->binding.value<invariants.size(),TreeStructureError::kOwnership);
    if(const auto& known=invariants[read->binding.value];known&&known->literal)return FlowPrimitiveReference{*known->literal};
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
  if(expressions.completed_reads.empty())return SolveConstants(flow,live,values,expressions,primitives,texts,allowance);
  try {
    TreeCheckBudget budget(allowance);
    RequireTree(expressions.completed_reads.size()==expressions.size(),TreeStructureError::kOwnership);
    // Freeze binding invariants before contextual refinement. They prove all
    // retained definitions/entry demand and survive conservative unreachable
    // incoming facts. A provisional contextual value must never erase that
    // independent stronger proof. Both domains consume the same equations.
    auto initial=SolveConstants(flow,live,values,
        {expressions.base,expressions.replacements,expressions.bindings,{},expressions.read_values,{},expressions.transports},primitives,texts,budget.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&initial))throw *error;
    auto invariant=std::get<FlowConstants>(std::move(initial));
    budget.Admit(invariant.allocated_bytes,1);budget.Step(invariant.work);
    const FlowValueEquations refined{expressions.base,expressions.replacements,expressions.bindings,
        expressions.completed_reads,expressions.read_values,invariant.values,expressions.transports};
    bool candidate=false;
    for(const auto& reference:flow.references) {
      budget.Step();const auto* id=std::get_if<ExprId>(&reference.site);
      if(reference.kind!=FlowReferenceKind::kRead||!id)continue;
      RequireTree(id->value<refined.size()&&reference.binding.value<flow.bindings.size(),TreeStructureError::kOwnership);
      candidate|=expressions.completed_reads[id->value]&&flow.bindings[reference.binding.value].exposure==FlowBindingExposure::kPrivate&&
          std::holds_alternative<FlowBindingValue>(refined[id->value]);
    }
    if(!candidate){invariant.allocated_bytes=budget.Bytes();invariant.work=budget.Work();return invariant;}
    auto analyzed=SolveConstants(flow,live,values,refined,primitives,texts,budget.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
    auto result=std::get<FlowConstants>(std::move(analyzed));
    budget.Admit(result.allocated_bytes,1);budget.Step(result.work);
    for(std::size_t i=0;i<result.values.size();++i){budget.Step();if(invariant.values[i])result.values[i]=invariant.values[i];}
    // Symbolic binding substitution requires the all-definition invariant;
    // contextual primitive reads cannot manufacture global storage identity.
    result.bindings=std::move(invariant.bindings);
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
namespace {
std::variant<FlowConstants,TreeStructureError> SolveConstants(
    const RecoveryFlow& flow,const FlowLiveness& live,std::span<const FlowDefinitionValue> values,
    FlowValueEquations expressions,std::span<const TreeLiteral> primitives,
    std::span<const std::u16string> texts,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);
    RequireTree(values.size()==flow.definitions.size()&&live.live_definitions.size()==values.size(),TreeStructureError::kOwnership);
    RequireTree(expressions.replacements.empty()||expressions.replacements.size()==expressions.size(),TreeStructureError::kOwnership);
    RequireTree(expressions.completed_reads.empty()||expressions.completed_reads.size()==expressions.size(),TreeStructureError::kOwnership);
    RequireTree(expressions.read_values.empty()||expressions.read_values.size()==expressions.size(),TreeStructureError::kOwnership);
    RequireTree(expressions.transports.empty()||expressions.transports.size()==expressions.size(),TreeStructureError::kOwnership);
    struct ValueId {std::uint32_t value;};
    struct DependencyId {std::uint32_t value;};
    struct Dependency {ValueId target;std::optional<DependencyId> next;};
    enum class ValueKind : std::uint8_t {kUnresolved,kPrimitive,kBinding,kUnknown};
    // Terminal values need no mutable node, and binding reads use their binding
    // node directly. This contracts identities without discarding any operand.
    struct Value {ValueKind kind=ValueKind::kUnresolved;const TreeLiteral* primitive=nullptr;std::optional<PrimitiveId> literal;std::optional<SymbolId> root;};
    const auto bindings=flow.bindings.size();
    std::vector<std::uint8_t> stable;budget.Array(stable,bindings);
    for(std::size_t i=0;i<bindings;++i){budget.Step();const auto& source=flow.bindings[i];
      if(source.stable_entry)RequireTree(source.kind==BindingKind::kParameter||source.kind==BindingKind::kFunctionSelf,TreeStructureError::kBinding);
      stable[i]=source.stable_entry;}
    // A stable entry identity is grounded only while every retained definition
    // is its entry initialization. A body write, even dead or unreachable, may
    // change a binding-valued result and defeats this all-definition invariant.
    for(const auto& definition:flow.definitions){budget.Step();RequireTree(definition.binding.value<bindings,TreeStructureError::kBinding);
      if(definition.kind!=FlowDefinitionKind::kParameter&&definition.kind!=FlowDefinitionKind::kFunctionSelf)stable[definition.binding.value]=0;}
    for(const auto& reference:flow.references){budget.Step();RequireTree(reference.binding.value<bindings,TreeStructureError::kBinding);
      if(reference.kind==FlowReferenceKind::kWrite)stable[reference.binding.value]=0;}

    RequireTree(expressions.size()<=std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
    const auto transfer=[](const FlowValueEquation& equation){return !std::holds_alternative<UnknownFlowValue>(equation)&&!std::holds_alternative<FlowPrimitiveReference>(equation)&&!std::holds_alternative<FlowBindingValue>(equation);};
    std::size_t active=0;
    for(std::size_t i=0;i<expressions.size();++i){budget.Step();active+=transfer(expressions[i]);}
    const auto fact_begin=bindings+active;
    RequireTree(fact_begin>=bindings&&fact_begin<=std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
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
    // Specialize normal values at actual completed private reads. Their sparse
    // incoming liveness facts are shared by every consumer; no read x definition
    // product is materialized. Observable cells keep invariant/unknown values.
    const auto read_site=[&](const FlowReference& reference)->std::optional<ExprId> {
      if(expressions.completed_reads.empty()||reference.kind!=FlowReferenceKind::kRead)return {};
      const auto* id=std::get_if<ExprId>(&reference.site);if(!id)return {};
      RequireTree(id->value<expressions.size()&&reference.binding.value<bindings,TreeStructureError::kOwnership);
      const auto equation=expressions[id->value];const auto* read=std::get_if<FlowBindingValue>(&equation);
      if(!read||!expressions.completed_reads[id->value]||flow.bindings[reference.binding.value].exposure!=FlowBindingExposure::kPrivate)return {};
      RequireTree(read->binding==reference.binding,TreeStructureError::kOwnership);
      return *id;
    };
    std::vector<std::uint8_t> read_bindings;budget.Array(read_bindings,bindings);
    bool occurrence_values=false;
    for(const auto& reference:flow.references){budget.Step();if(read_site(reference)){read_bindings[reference.binding.value]=1;occurrence_values=true;}}
    struct LiveFactId {std::uint32_t value;};
    std::vector<LiveFactId> fact_ids;
    if(occurrence_values) {
      RequireTree(live.live_in.size()<=std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
      std::size_t count=0;std::optional<LiveBinding> previous;
      for(const auto& fact:live.live_in) {
        budget.Step();RequireTree(fact.binding.value<bindings&&fact.point.value<flow.events.size()&&(!previous||*previous<fact),TreeStructureError::kOwnership);
        previous=fact;count+=read_bindings[fact.binding.value];
      }
      budget.Array(fact_ids,count);std::size_t next=0;
      for(std::size_t i=0;i<live.live_in.size();++i){budget.Step();if(read_bindings[live.live_in[i].binding.value])fact_ids[next++]={static_cast<std::uint32_t>(i)};}
    }
    const auto read_begin=fact_begin+fact_ids.size();std::size_t size=read_begin;
    RequireTree(read_begin>=fact_begin&&read_begin<=std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
    for(const auto& reference:flow.references) {
      budget.Step();if(const auto id=read_site(reference)) {
        auto& node=expression_nodes[id->value];RequireTree(node.has_value(),TreeStructureError::kOwnership);
        if(node->value<bindings) {
          RequireTree(size<std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
          node=ValueId{static_cast<std::uint32_t>(size++)};
        }else RequireTree(node->value>=read_begin,TreeStructureError::kOwnership);
      }
    }
    const auto fact_node=[&](LiveBinding fact)->std::optional<ValueId> {
      const auto found=std::lower_bound(fact_ids.begin(),fact_ids.end(),fact,[&](LiveFactId id,const LiveBinding& key){budget.Step();return live.live_in[id.value]<key;});
      if(found==fact_ids.end()||live.live_in[found->value]!=fact)return {};
      return ValueId{static_cast<std::uint32_t>(fact_begin+(found-fact_ids.begin()))};
    };
    const auto fact_inputs=[&](LiveFactId id,const auto& node,const auto& definition) {
      const auto& fact=live.live_in[id.value];const auto& event=flow.events[fact.point.value];
      if(fact.point==flow.entry||!event.incoming)node(std::optional<ValueId>{});
      for(auto edge=event.incoming;edge;edge=flow.edges[*edge].incoming) {
        budget.Step();const auto predecessor=flow.edges[*edge].from;const auto defined=flow.events[predecessor.value].definition;
        if(defined&&flow.definitions[defined->value].binding==fact.binding)definition(values[defined->value]);
        else node(fact_node({predecessor,fact.binding}));
      }
    };
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
    for(const auto id:fact_ids){budget.Step();fact_inputs(id,count_input,[&](const auto& value){inputs(value,count_input);});}
    for(const auto& reference:flow.references){budget.Step();if(read_site(reference))count_input(fact_node({reference.point,reference.binding}));}
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
      if(a.kind!=b.kind)return unknown;
      if(a.kind==ValueKind::kBinding)return a.root==b.root?a:unknown;
      if(!SamePrimitiveValue(*a.primitive,*b.primitive))return unknown;
      if(!a.literal)a.literal=b.literal;
      return a;
    };
    const auto update=[&](ValueId id,const Value& value) {
      budget.Step();auto& current=states[id.value];const auto merged=join(current,value);
      if(merged.kind==current.kind&&(merged.kind!=ValueKind::kPrimitive||merged.literal==current.literal)&&
          (merged.kind!=ValueKind::kBinding||merged.root==current.root))return;
      current=merged;if(!queued[id.value]){queued[id.value]=1;pending[ready++]=id;}
    };
    const auto connect_value=[&](const FlowDefinitionValue& value,ValueId target) {
      std::visit([&](const auto& source) {
        using T=std::decay_t<decltype(source)>;
        if constexpr(std::is_same_v<T,FlowBindingValue>)depend(binding(source.binding),target);
        else if constexpr(std::is_same_v<T,FlowExpressionValue>){if(const auto id=expression(source.expression))depend(*id,target);else update(target,read_expression(source.expression));}
        else if constexpr(std::is_same_v<T,FlowPrimitiveValue>)update(target,primitive(source));
        else {static_assert(std::is_same_v<T,UnknownFlowValue>);update(target,unknown);}
      },value);
    };
    for(std::size_t i=0;i<bindings;++i){budget.Step();if(stable[i])update(binding({static_cast<std::uint32_t>(i)}),{ValueKind::kBinding,nullptr,{},SymbolId{static_cast<std::uint32_t>(i)}});}
    for(std::size_t i=0;i<values.size();++i){budget.Step();if(retained(i)&&!stable[flow.definitions[i].binding.value])connect_value(values[i],binding(flow.definitions[i].binding));}
    for(std::size_t i=0;i<fact_ids.size();++i) {
      budget.Step();const ValueId target{static_cast<std::uint32_t>(fact_begin+i)};
      fact_inputs(fact_ids[i],[&](std::optional<ValueId> source){if(source)depend(*source,target);else update(target,unknown);},
          [&](const FlowDefinitionValue& value){connect_value(value,target);});
    }
    for(const auto& reference:flow.references) {
      budget.Step();if(const auto id=read_site(reference)) {
        const auto target=*expression(*id);const auto source=fact_node({reference.point,reference.binding});
        if(source)depend(*source,target);else update(target,unknown);
      }
    }
    for(const auto id:equations) {
      budget.Step();const auto target=*expression(id);
      inputs(expressions[id.value],[&](std::optional<ValueId> source){if(source)depend(*source,target);});
    }
    for(std::size_t i=0;i<bindings;++i){budget.Step();if(flow.bindings[i].exposure!=FlowBindingExposure::kPrivate&&!stable[i])update(binding({static_cast<std::uint32_t>(i)}),unknown);}
    for(const auto& fact:live.live_in){budget.Step();if(fact.point==flow.entry&&!stable[fact.binding.value])update(binding(fact.binding),unknown);}
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
            if(a.kind!=ValueKind::kUnresolved&&a.kind!=ValueKind::kUnknown&&a.kind==b.kind&&join(a,b).kind!=ValueKind::kUnknown)return join(a,b);
            return {};
          }
          return join(a,b);
        }else if constexpr(std::is_same_v<T,FlowLogicalValue>) {
          const auto left=read_expression(value.left);
          if(left.kind==ValueKind::kBinding)return join(left,read_expression(value.right));
          if(left.kind!=ValueKind::kPrimitive)return left;
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
    for(std::size_t i=bindings;i<fact_begin;++i){budget.Step();const ValueId id{static_cast<std::uint32_t>(i)};update(id,evaluate(id));}
    // Flat constant lattice (Wegman/Zadeck value propagation), without changing
    // executable edges. Each node advances bottom->known->unknown at most;
    // a printable identity adds at most one notification. Incremental joins
    // avoid rescanning all definitions when one dependency changes.
    const auto solve=[&] {
      while(ready) {
        budget.Step();const auto source=pending[--ready];queued[source.value]=0;
        for(auto edge=heads[source.value];edge;edge=dependencies[edge->value].next) {
          budget.Step();const auto target=dependencies[edge->value].target;
          update(target,target.value<bindings||target.value>=fact_begin?states[source.value]:evaluate(target));
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
    FlowConstants result;budget.Array(result.values,bindings);budget.Array(result.bindings,bindings);
    std::size_t read_count=0;
    for(const auto node:expression_nodes){budget.Step();if(node&&node->value>=read_begin&&states[node->value].kind==ValueKind::kPrimitive&&states[node->value].literal)++read_count;}
    budget.Array(result.reads,read_count);std::size_t read_next=0;
    for(std::size_t i=0;i<expression_nodes.size();++i) {
      budget.Step();const auto node=expression_nodes[i];
      if(node&&node->value>=read_begin&&states[node->value].kind==ValueKind::kPrimitive&&states[node->value].literal)
        result.reads[read_next++]={{static_cast<std::uint32_t>(i)},*states[node->value].literal};
    }
    for(std::size_t i=0;i<bindings;++i){budget.Step();if(states[i].kind==ValueKind::kPrimitive)result.values[i]=FlowPrimitiveValue{*states[i].primitive,states[i].literal};
      else if(states[i].kind==ValueKind::kBinding)result.bindings[i]=states[i].root;}
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
}
