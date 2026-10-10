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
    std::size_t caught=0;
    for(const auto& definition:flow.definitions){budget.Step();if(definition.kind==FlowDefinitionKind::kCatch)++caught;}
    RequireTree(expressions<=std::numeric_limits<std::uint32_t>::max()&&
        facts<=std::numeric_limits<std::uint32_t>::max()-expressions&&
        caught<=std::numeric_limits<std::uint32_t>::max()-expressions-facts&&
        events<=std::numeric_limits<std::uint32_t>::max()-expressions-facts-caught,Error::kResourceLimit);
    struct NodeId {std::uint32_t value;};
    enum class DependencyKind { kValue, kCompletion };
    struct Dependency {
      NodeId target;std::optional<std::uint32_t> next;
      DependencyKind kind;const FlowDefinition* definition;
    };
    const auto value_count=expressions+facts+caught,count=value_count+events;
    const auto event_node=[&](FlowPoint point){return NodeId{static_cast<std::uint32_t>(value_count+point.value)};};
    std::vector<K> values,entries,original_results;
    std::vector<std::optional<std::uint32_t>> heads;
    std::vector<std::uint8_t> queued,modeled_reads,changed,original_entries;
    std::vector<NodeId> pending;
    std::vector<std::optional<NodeId>> catch_values,catch_entries;
    std::vector<FlowDefinitionId> catch_roots;
    budget.Array(catch_values,caught?flow.definitions.size():0);budget.Array(catch_entries,caught?events:0);
    budget.Array(catch_roots,caught);std::size_t root_count=0;
    std::size_t next_catch=expressions+facts;
    for(std::size_t i=0;i<flow.definitions.size();++i) {
      budget.Step();if(flow.definitions[i].kind!=FlowDefinitionKind::kCatch)continue;
      const auto entry=flow.definitions[i].catch_entry;
      if(entry)RequireTree(entry->value<events,Error::kOwnership);
      if(entry&&catch_entries[entry->value])catch_values[i]=catch_entries[entry->value];
      else {
        catch_values[i]=NodeId{static_cast<std::uint32_t>(next_catch++)};
        if(entry)catch_entries[entry->value]=catch_values[i];
        catch_roots[root_count++]={static_cast<std::uint32_t>(i)};
      }
    }
    budget.Array(values,value_count);budget.Array(heads,count);budget.Array(queued,value_count);budget.Array(pending,count);
    budget.Array(modeled_reads,expressions);budget.Array(entries,flow.bindings.size());
    budget.Array(changed,count);budget.Array(original_entries,flow.bindings.size());
    budget.Array(original_results,expressions);
    RequireTree(flow.original_results.empty()||flow.original_results.size()==input.original->operations.size(),Error::kOwnership);
    for(std::size_t i=0;i<flow.original_results.size();++i) {
      budget.Step();const auto& occurrence=flow.original_results[i];if(!occurrence)continue;
      const auto& operation=input.original->operations[i];
      RequireTree(operation.result&&operation.expression==occurrence->expression&&
          occurrence->expression.value<expressions&&occurrence->entry.value<events&&occurrence->normal.value<events&&
          operation.result->value<input.original->values.size(),Error::kOwnership);
      auto& kinds=original_results[occurrence->expression.value];
      kinds=kinds.Join(input.original->values[operation.result->value].domain);
    }
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
    const auto entry_domain=[&](SymbolId binding) {
      RequireTree(binding.value<flow.bindings.size(),Error::kOwnership);
      const auto& source=flow.bindings[binding.value];
      return source.exposure==FlowBindingExposure::kPrivate||source.stable_entry;
    };
    const auto tracked_binding=[&](SymbolId binding) {
      // Value tracking is not permission to erase observable writes. Exact
      // source allocation/exit transfers identify a current scoped unit;
      // externally mutable units lose precision at actual user-code completions.
      return entry_domain(binding)||flow.BindingUnit(binding)==FlowUnitCapability::kCurrent;
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
      // A pending exception is an explicit join, not the normal result of the
      // throwing operation. One sparse node per catch shares value dependencies
      // with ordinary definitions; implicit failures conservatively contribute
      // Language. Cleanup precision gaps cannot invent a known payload.
      for(std::size_t root=0;root<root_count;++root) {
        budget.Step();const auto i=catch_roots[root].value;
        const auto target=*catch_values[i];const auto& definition=flow.definitions[i];
        if(!definition.catch_entry){constant(target,K::Language());invalidate(target);continue;}
        RequireTree(definition.catch_entry->value<events,Error::kOwnership);
        dependency(event_node(*definition.catch_entry),target,DependencyKind::kCompletion,nullptr);
        bool input_seen=false;
        for(auto edge=flow.events[definition.catch_entry->value].incoming;edge;edge=flow.edges[*edge].incoming) {
          budget.Step();const auto& incoming=flow.edges[*edge];if(!live.reachable[incoming.from.value])continue;
          input_seen=true;const auto& source=flow.events[incoming.from.value];
          if(incoming.completion==FlowCompletion::kThrow&&source.thrown_value) {
            RequireTree(source.thrown_value->value<expressions,Error::kOwnership);
            dependency(NodeId{source.thrown_value->value},target,DependencyKind::kValue,nullptr);
          } else {constant(target,K::Language());invalidate(target);}
        }
        if(!input_seen){constant(target,K::Language());invalidate(target);}
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
      // A result range can depend on the path reaching its evaluation. This
      // edge joins the shared invalidation graph; predicates are completion
      // dependencies, never value operands or a per-operation backward walk.
      for(const auto& occurrence:flow.original_results) {
        budget.Step();if(!occurrence)continue;
        dependency(event_node(occurrence->entry),NodeId{occurrence->expression.value},DependencyKind::kCompletion,nullptr);
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
        if(tracked_binding(reference.binding)) {
          if(const auto source=fact_node({reference.point,reference.binding})){dependency(*source,target,DependencyKind::kValue,nullptr);continue;}
        }
        constant(target,K::Language());invalidate(target);
      }
      for(std::size_t i=0;i<facts;++i) {
        budget.Step();const auto& fact=live.live_in[i];
        RequireTree(fact.point.value<flow.events.size()&&fact.binding.value<flow.bindings.size()&&
            (i==0||live.live_in[i-1]<fact),Error::kOwnership);
        const NodeId target{static_cast<std::uint32_t>(expressions+i)};
        if(!tracked_binding(fact.binding)||!live.reachable[fact.point.value]){constant(target,K::Language());invalidate(target);continue;}
        const auto& event=flow.events[fact.point.value];
        if(fact.point==flow.entry||!event.incoming){constant(target,K::Language());invalidate(target);}
        for(auto edge=event.incoming;edge;edge=flow.edges[*edge].incoming) {
          budget.Step();const auto predecessor=flow.edges[*edge].from;
          if(!live.reachable[predecessor.value])continue;
          if(flow.TransferBinding(fact.binding,flow.edges[*edge])!=FlowUnitTransfer::kPreserved){constant(target,K::Language());invalidate(target);continue;}
          const auto& prior=flow.events[predecessor.value];
          const auto definition=prior.definition;
          if(definition) {
            RequireTree(definition->value<flow.definitions.size(),Error::kOwnership);
            const auto& generated=flow.definitions[definition->value];
            if(generated.binding==fact.binding) {
              switch(generated.kind) {
                case FlowDefinitionKind::kIterationCopy:
                  if(generated.input)if(const auto source=fact_node({generated.input->point,generated.input->binding})) {
                    dependency(*source,target,DependencyKind::kValue,nullptr);continue;
                  }
                  break;
                case FlowDefinitionKind::kAssignment:case FlowDefinitionKind::kInitializer:
                  if(generated.value){RequireTree(generated.value->value<expressions,Error::kOwnership);dependency(NodeId{generated.value->value},target,DependencyKind::kValue,&generated);
                    dependency(event_node(generated.point),target,DependencyKind::kCompletion,nullptr);
                    if(generated.write)dependency(NodeId{generated.write->value},target,DependencyKind::kCompletion,nullptr);
                    if(!generated.original)invalidate(target);
                    continue;}
                  break;
                case FlowDefinitionKind::kParameter: {
                  // Current body-cell tracking cannot lend a signature proof.
                  // Entry types and their raw dependency cone require the
                  // separate adapter certificate, including mapped/defaults.
                  const bool admitted=entry_domain(fact.binding)&&original_entries[fact.binding.value];
                  constant(target,admitted&&!entries[fact.binding.value].IsEmpty()?entries[fact.binding.value]:K::Language());
                  if(!admitted)invalidate(target);
                  continue;
                }
                case FlowDefinitionKind::kCatch:
                  dependency(event_node(generated.point),target,DependencyKind::kCompletion,nullptr);
                  RequireTree(catch_values[definition->value].has_value(),Error::kOwnership);
                  dependency(*catch_values[definition->value],target,DependencyKind::kValue,&generated);
                  continue;
                case FlowDefinitionKind::kUndefined:case FlowDefinitionKind::kUndefinedStore:
                case FlowDefinitionKind::kFunctionSelf:case FlowDefinitionKind::kUpdate:break;
              }
              constant(target,K::Language());invalidate(target);continue;
            }
          }
          // The actual completion's user effects can replace exposed units.
          // Retained guards, allocation and other declarative writes cannot.
          // Successful stores above independently generate their new value.
          if(flow.bindings[fact.binding.value].MayBeChangedByUserCode()&&prior.MayInvokeUserCode(flow.edges[*edge].completion)) {
            constant(target,K::Language());invalidate(target);continue;
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
    // Completion edits are produced by the current/trial adapter, including
    // omitted non-value actions. Reading a plan here duplicated adaptation and
    // could revive raw proofs when a caller retained canonical value equations.
    for(std::size_t i=0;i<events;++i) {
      budget.Step();if(flow.events[i].provenance!=FlowProvenance::kPreserved)
        changed[event_node(FlowPoint{static_cast<std::uint32_t>(i)}).value]=1;
    }
    const bool boundaries=flow.statements.size()==tree.statements.size()&&flow.expressions.size()==expressions;
    for(std::size_t i=0;i<expressions;++i) {
      budget.Step();
      if(boundaries) {
        RequireTree(flow.expressions[i].value<events,Error::kOwnership);
        if(flow.events[flow.expressions[i].value].provenance!=FlowProvenance::kPreserved)changed[i]=1;
      }
      if(std::holds_alternative<FlowBindingValue>(ProjectedRecoveryEquation(input,ExprId{static_cast<std::uint32_t>(i)}))&&!modeled_reads[i])changed[i]=1;
    }
    // Value-only graph clients can still solve current source domains. Missing
    // boundary provenance withdraws raw path precision without rejecting them.
    if(!boundaries)for(std::size_t i=0;i<events;++i) {
      budget.Step();changed[event_node(FlowPoint{static_cast<std::uint32_t>(i)}).value]=1;
    }
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
      auto incoming=std::visit([&](const auto& value)->K {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,FlowExpressionValue>)return values[value.expression.value];
        else if constexpr(std::is_same_v<T,FlowChoiceValue>)return values[value.consequent.value].Join(values[value.alternate.value]);
        else if constexpr(std::is_same_v<T,FlowLogicalValue>)return values[value.left.value].Join(values[value.right.value]);
        else if constexpr(std::is_same_v<T,FlowPrimitiveReference>) {
          RequireTree(value.primitive.value<input.bindings.primitives.size(),Error::kOwnership);return PrimitiveKinds(input.bindings.primitives[value.primitive.value]);
        } else if constexpr(std::is_same_v<T,FlowBindingValue>)return modeled_reads[target.value]?K{}:K::Language();
        else {static_assert(std::is_same_v<T,UnknownFlowValue>);return NormalValueKinds(tree.expressions[target.value].expression,{values.data(),expressions});}
      },equation);
      const auto original=original_results[target.value];
      // Inline results and stored results use the same operand/completion
      // validity. Do not use normal_kinds: it also contains operand hints.
      if(!changed[target.value]&&!original.IsEmpty()&&original.Join(incoming)==incoming)incoming=original;
      return incoming;
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
          if(dependency.definition&&!changed[source.value]&&!changed[target.value]) {
            K original;
            if(dependency.definition->original)original=OriginalDefinitionKinds(*input.original,*dependency.definition->original);
            if(const auto* caught=dependency.definition->original_catch) {
              RequireTree(caught->value.value<input.original->values.size(),Error::kOwnership);
              original=input.original->values[caught->value.value].domain;
            }
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
