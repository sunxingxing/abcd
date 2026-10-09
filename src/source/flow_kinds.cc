#include "flow_kinds.h"
#include "recovery_projection.h"
#include "expression_children.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace abcd::source {
std::variant<FlowNormalKinds,TreeStructureError> AnalyzeFlowNormalKinds(
    const FunctionTree& tree,const RecoveryFlow& flow,const FlowLiveness& live,
    const RecoveryExpressionInput& input,analysis::SemanticBudget allowance) {
  using K=analysis::ValueKinds;using Error=TreeStructureError;
  try {
    TreeCheckBudget budget(allowance);
    RequireTree(input.original&&flow.original==input.original&&live.reachable.size()==flow.events.size(),Error::kOwnership);
    CheckOriginalFlow(tree,*input.original);
    const auto expressions=tree.expressions.size(),facts=live.live_in.size(),events=flow.events.size();
    RequireTree(expressions<=std::numeric_limits<std::uint32_t>::max()&&
        facts<=std::numeric_limits<std::uint32_t>::max()-expressions&&
        events<=std::numeric_limits<std::uint32_t>::max()-expressions-facts,Error::kResourceLimit);
    struct NodeId {std::uint32_t value;};
    enum class DependencyKind { kValue, kCompletion };
    struct Dependency {
      NodeId target;std::optional<std::uint32_t> next;
      DependencyKind kind;const FlowDefinition* definition;
    };
    const auto value_count=expressions+facts,count=value_count+events;
    const auto event_node=[&](FlowPoint point){return NodeId{static_cast<std::uint32_t>(value_count+point.value)};};
    std::vector<K> values,entries;
    std::vector<std::optional<std::uint32_t>> heads;
    std::vector<std::uint8_t> queued,modeled_reads,changed,original_entries;
    std::vector<NodeId> pending;
    budget.Array(values,value_count);budget.Array(heads,count);budget.Array(queued,value_count);budget.Array(pending,count);
    budget.Array(modeled_reads,expressions);budget.Array(entries,flow.bindings.size());
    budget.Array(changed,count);budget.Array(original_entries,flow.bindings.size());
    // Entry domain is a type proof from the verified original SSA, not an
    // immutable value/cell identity. Mapped arguments/captures remain unknown
    // through the adapter's exposure/stable-entry contract.
    for(const auto& value:input.original->values) {
      budget.Step();const auto* entry=std::get_if<analysis::EntryDefinition>(&value.origin);
      const auto* parameter=entry?std::get_if<analysis::ParameterEntry>(entry):nullptr;
      if(!parameter||value.life!=analysis::ValueLife::kLive)continue;
      RequireTree(parameter->index<tree.parameters.size(),Error::kOwnership);
      const auto binding=input.bindings.Storage(tree.parameters[parameter->index].symbol);
      RequireTree(binding.value<entries.size(),Error::kOwnership);
      entries[binding.value]=entries[binding.value].Join(value.domain);original_entries[binding.value]=1;
    }
    const auto fact_node=[&](LiveBinding key)->std::optional<NodeId> {
      const auto found=std::lower_bound(live.live_in.begin(),live.live_in.end(),key,
          [&](const auto& a,const auto& b){budget.Step();return a<b;});
      if(found==live.live_in.end()||*found!=key)return {};
      return NodeId{static_cast<std::uint32_t>(expressions+(found-live.live_in.begin()))};
    };
    const auto private_binding=[&](SymbolId binding) {
      RequireTree(binding.value<flow.bindings.size(),Error::kOwnership);
      const auto& source=flow.bindings[binding.value];
      return source.exposure==FlowBindingExposure::kPrivate||source.stable_entry;
    };
    std::vector<std::optional<SymbolId>> writes;budget.Array(writes,flow.events.size());
    for(const auto& reference:flow.references) {
      budget.Step();RequireTree(reference.point.value<flow.events.size(),Error::kOwnership);
      if(reference.kind==FlowReferenceKind::kWrite) {
        auto& write=writes[reference.point.value];RequireTree(!write||*write==reference.binding,Error::kOwnership);write=reference.binding;
      }
    }
    // Each input is an expression edge or a sparse incoming liveness fact.
    // No read x definition relation, per-binding graph walk or layout test.
    const auto inputs=[&](const auto& constant,const auto& dependency,const auto& invalidate) {
      // Execution context is one shared completion graph, not a backward walk
      // for each definition. Range proofs may depend on prior path predicates.
      for(std::size_t i=0;i<events;++i) {
        budget.Step();const auto& event=flow.events[i];
        const auto target=event_node(FlowPoint{static_cast<std::uint32_t>(i)});
        if(event.expression)dependency(NodeId{event.expression->value},target,DependencyKind::kCompletion,nullptr);
        for(auto edge=event.incoming;edge;edge=flow.edges[*edge].incoming) {
          budget.Step();dependency(event_node(flow.edges[*edge].from),target,DependencyKind::kCompletion,nullptr);
        }
      }
      for(std::size_t i=0;i<expressions;++i) {
        budget.Step();const NodeId target{static_cast<std::uint32_t>(i)};
        const auto equation=ProjectedRecoveryEquation(input,ExprId{target.value});
        std::visit([&](const auto& value) {
          using T=std::decay_t<decltype(value)>;
          const auto expression=[&](ExprId id){RequireTree(id.value<expressions,Error::kOwnership);dependency(NodeId{id.value},target,DependencyKind::kValue,nullptr);};
          if constexpr(std::is_same_v<T,FlowExpressionValue>)expression(value.expression);
          else if constexpr(std::is_same_v<T,FlowChoiceValue>){expression(value.consequent);expression(value.alternate);
            dependency(NodeId{value.condition.value},target,DependencyKind::kCompletion,nullptr);}
          else if constexpr(std::is_same_v<T,FlowLogicalValue>){expression(value.left);expression(value.right);}
          else if constexpr(std::is_same_v<T,UnknownFlowValue>)
            VisitExpressionChildren(tree,tree.expressions[i].expression,expression);
          else static_assert(std::is_same_v<T,FlowPrimitiveReference>||std::is_same_v<T,FlowBindingValue>);
        },equation);
      }
      for(const auto& reference:flow.references) {
        budget.Step();const auto* expression=std::get_if<ExprId>(&reference.site);
        if(reference.kind!=FlowReferenceKind::kRead||!expression)continue;
        RequireTree(expression->value<expressions,Error::kOwnership);
        if(!live.reachable[reference.point.value])continue;
        const auto equation=ProjectedRecoveryEquation(input,*expression);
        const auto* read=std::get_if<FlowBindingValue>(&equation);
        if(!read)continue;
        RequireTree(read->binding==reference.binding,Error::kOwnership);
        const NodeId target{expression->value};modeled_reads[expression->value]=1;
        if(private_binding(reference.binding)) {
          if(const auto source=fact_node({reference.point,reference.binding})){dependency(*source,target,DependencyKind::kValue,nullptr);continue;}
        }
        constant(target,K::Language());invalidate(target);
      }
      for(std::size_t i=0;i<facts;++i) {
        budget.Step();const auto& fact=live.live_in[i];
        RequireTree(fact.point.value<flow.events.size()&&fact.binding.value<flow.bindings.size()&&
            (i==0||live.live_in[i-1]<fact),Error::kOwnership);
        const NodeId target{static_cast<std::uint32_t>(expressions+i)};
        if(!private_binding(fact.binding)||!live.reachable[fact.point.value]){constant(target,K::Language());invalidate(target);continue;}
        const auto& event=flow.events[fact.point.value];
        if(fact.point==flow.entry||!event.incoming){constant(target,K::Language());invalidate(target);}
        for(auto edge=event.incoming;edge;edge=flow.edges[*edge].incoming) {
          budget.Step();const auto predecessor=flow.edges[*edge].from;
          if(!live.reachable[predecessor.value])continue;
          if(flow.ResetsBinding(fact.binding,predecessor)){constant(target,K::Language());invalidate(target);continue;}
          const auto& prior=flow.events[predecessor.value];
          const auto definition=prior.definition;
          if(definition) {
            RequireTree(definition->value<flow.definitions.size(),Error::kOwnership);
            const auto& generated=flow.definitions[definition->value];
            if(generated.binding==fact.binding) {
              switch(generated.kind) {
                case FlowDefinitionKind::kAssignment:case FlowDefinitionKind::kInitializer:
                  if(generated.value){RequireTree(generated.value->value<expressions,Error::kOwnership);dependency(NodeId{generated.value->value},target,DependencyKind::kValue,&generated);
                    dependency(event_node(generated.point),target,DependencyKind::kCompletion,nullptr);
                    if(generated.write)dependency(NodeId{generated.write->value},target,DependencyKind::kCompletion,nullptr);
                    if(!generated.original)invalidate(target);
                    continue;}
                  break;
                case FlowDefinitionKind::kParameter:
                  constant(target,entries[fact.binding.value].IsEmpty()?K::Language():entries[fact.binding.value]);
                  if(!original_entries[fact.binding.value])invalidate(target);
                  continue;
                case FlowDefinitionKind::kUndefined:case FlowDefinitionKind::kUndefinedStore:
                case FlowDefinitionKind::kFunctionSelf:case FlowDefinitionKind::kUpdate:case FlowDefinitionKind::kCatch:break;
              }
              constant(target,K::Language());invalidate(target);continue;
            }
          }
          if(writes[predecessor.value]==fact.binding){constant(target,K::Language());invalidate(target);continue;}
          if(const auto source=fact_node({predecessor,fact.binding}))dependency(*source,target,DependencyKind::kValue,nullptr);
          else {constant(target,K::Language());invalidate(target);}
        }
      }
    };
    std::size_t edges=0;
    inputs([](NodeId,K){},[&](NodeId,NodeId,DependencyKind,const FlowDefinition*){++edges;},[](NodeId){});
    RequireTree(edges<=std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
    std::vector<Dependency> dependencies;budget.Array(dependencies,edges);
    std::size_t next=0,ready=0;
    const auto update=[&](NodeId id,K incoming) {
      budget.Step();auto& value=values[id.value];const auto joined=value.Join(incoming);
      if(joined==value)return;
      value=joined;if(!queued[id.value]){queued[id.value]=1;pending[ready++]=id;}
    };
    inputs([](NodeId,K){},[&](NodeId source,NodeId target,DependencyKind kind,const FlowDefinition* definition) {
      const auto id=static_cast<std::uint32_t>(next++);dependencies[id]={target,heads[source.value],kind,definition};heads[source.value]=id;
    },[&](NodeId id){changed[id.value]=1;});
    // A raw successful-definition domain remains conditional on its entire
    // current operand/completion cone. Source edits, unknown cells and
    // unmodeled environments invalidate once and propagate on the SAME sparse
    // graph; predicates order completion but never contribute their value type.
    for(std::size_t i=0;i<expressions;++i) {
      budget.Step();const auto& node=tree.expressions[i].expression;
      if(input.replacements[i]||input.transports[i]||input.bindings.read_sources[i]||input.bindings.read_literals[i]||
          IntrinsicLexicalDependency(node)!=LexicalDependency::kReferences)changed[i]=1;
      if(input.replacements[i])changed[input.replacements[i]->value]=1;
      if(const auto* read=std::get_if<SymbolExpr>(&node)) {
        const auto id=read->symbol.value;
        if(input.bindings.aliases[id]||input.bindings.storage[id]||input.bindings.read_bindings[id]||input.bindings.literals[id])changed[i]=1;
      }
      if(std::holds_alternative<FlowBindingValue>(ProjectedRecoveryEquation(input,ExprId{static_cast<std::uint32_t>(i)}))&&!modeled_reads[i])changed[i]=1;
    }
    // Even an omitted statement retains a linked boundary point, while its
    // current owner is absent. Seed the explicit projection delta there, so
    // removal of an earlier computation cannot silently retain a path proof.
    if(flow.statements.size()!=tree.statements.size()) {
      // A value-only graph can still solve source domains. Missing boundary
      // correspondence makes original path precision unavailable, not invalid
      // source semantics or a reason to reject the entire projection.
      for(std::size_t i=0;i<events;++i){budget.Step();changed[event_node(FlowPoint{static_cast<std::uint32_t>(i)}).value]=1;}
    } else for(std::size_t i=0;i<flow.statements.size();++i) {
      budget.Step();RequireTree(flow.statements[i].value<events,Error::kOwnership);const auto& plan=input.bindings;
      if(plan.omitted[i]||plan.statements[i]||
          (!plan.statement_values.empty()&&plan.statement_values[i])||
          (!plan.forwarded_values.empty()&&plan.forwarded_values[i])||
          (!plan.loop_declarations.empty()&&plan.loop_declarations[i])||
          (!plan.undefined_stores.empty()&&plan.undefined_stores[i])||
          (!plan.declaration_values.empty()&&plan.declaration_values[i])||
          (!plan.declaration_completions.empty()&&plan.declaration_completions[i]))changed[event_node(flow.statements[i]).value]=1;
    }
    for(const auto& name:flow.names){budget.Step();changed[name.site.value]=1;}
    for(std::size_t i=0;i<count;++i){budget.Step();if(changed[i])pending[ready++]={static_cast<std::uint32_t>(i)};}
    while(ready) {
      budget.Step();const auto source=pending[--ready];
      for(auto edge=heads[source.value];edge;edge=dependencies[*edge].next) {
        budget.Step();const auto target=dependencies[*edge].target;
        if(!changed[target.value]){changed[target.value]=1;pending[ready++]=target;}
      }
    }
    inputs(update,[](NodeId,NodeId,DependencyKind,const FlowDefinition*){},[](NodeId){});
    const auto expression=[&](NodeId target) {
      const auto equation=ProjectedRecoveryEquation(input,ExprId{target.value});
      return std::visit([&](const auto& value)->K {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,FlowExpressionValue>)return values[value.expression.value];
        else if constexpr(std::is_same_v<T,FlowChoiceValue>)return values[value.consequent.value].Join(values[value.alternate.value]);
        else if constexpr(std::is_same_v<T,FlowLogicalValue>)return values[value.left.value].Join(values[value.right.value]);
        else if constexpr(std::is_same_v<T,FlowPrimitiveReference>) {
          RequireTree(value.primitive.value<input.bindings.primitives.size(),Error::kOwnership);return PrimitiveKinds(input.bindings.primitives[value.primitive.value]);
        } else if constexpr(std::is_same_v<T,FlowBindingValue>)return modeled_reads[target.value]?K{}:K::Language();
        else {static_assert(std::is_same_v<T,UnknownFlowValue>);return NormalValueKinds(tree.expressions[target.value].expression,{values.data(),expressions});}
      },equation);
    };
    for(std::size_t i=0;i<expressions;++i){budget.Step();const NodeId id{static_cast<std::uint32_t>(i)};update(id,expression(id));}
    const auto solve=[&] {
      while(ready) {
        budget.Step();const auto source=pending[--ready];queued[source.value]=0;
        for(auto edge=heads[source.value];edge;edge=dependencies[*edge].next) {
          budget.Step();const auto& dependency=dependencies[*edge];
          if(dependency.kind==DependencyKind::kCompletion)continue;
          const auto target=dependency.target;
          auto incoming=values[source.value];
          if(dependency.definition&&dependency.definition->original&&!changed[source.value]&&!changed[target.value]) {
            const auto original=OriginalDefinitionKinds(*input.original,*dependency.definition->original);
            // Reuse only a stricter proof. A broad raw phi domain must not
            // discard precision already proved by current source semantics.
            if(!original.IsEmpty()&&original.Join(incoming)==incoming)incoming=original;
          }
          const bool read=target.value<expressions&&std::holds_alternative<FlowBindingValue>(ProjectedRecoveryEquation(input,ExprId{target.value}));
          update(target,target.value>=expressions||read?incoming:expression(target));
        }
      }
    };
    solve();
    // Ungrounded cycles must not invent a numeric value. The lattice only
    // widens; work is bounded by dependency edges times finite domain height.
    for(std::size_t i=0;i<value_count;++i){budget.Step();if(values[i].IsEmpty())update(NodeId{static_cast<std::uint32_t>(i)},K::Language());}
    solve();
    FlowNormalKinds result;budget.Array(result.expressions,expressions);
    std::copy_n(values.begin(),expressions,result.expressions.begin());
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(Error error){return error;}
  catch(const std::bad_alloc&){return Error::kResourceLimit;}
  catch(const std::length_error&){return Error::kResourceLimit;}
}
}  // namespace abcd::source
