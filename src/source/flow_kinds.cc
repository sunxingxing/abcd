#include "flow_kinds.h"
#include "recovery_projection.h"
#include "expression_children.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace abcd::source {
namespace {
struct ValueStateView {
  std::span<const LiveBinding> live_in;
  std::span<const std::uint8_t> reachable;
  std::span<const std::optional<LiveBinding>> forwarded;
};
std::variant<FlowNormalKinds,TreeStructureError> SolveFlowNormalKinds(
    const FunctionTree& tree,const RecoveryFlow& flow,ValueStateView live,
    const RecoveryExpressionInput& input,analysis::SemanticBudget allowance,const FlowEffectInput* completion,const FlowInitializationProgram* initialization) {
  using K=analysis::ValueKinds;using Error=TreeStructureError;
  try {
    TreeCheckBudget budget(allowance);
    RequireTree(!initialization||(completion&&initialization->owner==&flow&&
        initialization->events==flow.events.size()&&initialization->completions==flow.edges.size()),Error::kOwnership);
    if(completion)RequireTree(completion->evaluations.size()==tree.expressions.size()&&
        completion->normal_stores.size()==tree.expressions.size()&&
        completion->assignments.size()==tree.expressions.size()&&completion->normal_kinds.empty(),Error::kOwnership);
    RequireTree(flow.original==input.original&&live.reachable.size()==flow.events.size(),Error::kOwnership);
    if(input.original)CheckOriginalFlow(tree,*input.original);
    const auto expressions=tree.expressions.size(),facts=live.live_in.size(),events=flow.events.size();
    std::size_t caught=0;bool provenance_required=false;
    for(const auto& definition:flow.definitions){
      budget.Step();if(definition.kind==FlowDefinitionKind::kCatch)++caught;
      provenance_required|=definition.original!=nullptr||definition.original_catch!=nullptr;
    }
    RequireTree(expressions<=std::numeric_limits<std::uint32_t>::max()&&
        facts<=std::numeric_limits<std::uint32_t>::max()-expressions&&
        caught<=std::numeric_limits<std::uint32_t>::max()-expressions-facts&&
        events<=std::numeric_limits<std::uint32_t>::max()-expressions-facts-caught,Error::kResourceLimit);
    struct NodeId {std::uint32_t value;};
    enum class DependencyKind { kValue, kNumeric, kProvenance, kEffects };
    struct Guard {std::optional<std::size_t> edge;bool user_code=false;};
    Guard guard;
    enum class ValueOccurrence : std::uint8_t {kUnmodeled,kRead,kNumeric};
    struct Dependency {
      NodeId target;std::optional<std::uint32_t> next;
      DependencyKind kind;const FlowDefinition* definition;Guard guard;NodeId source;
      std::optional<std::uint32_t> guarded_next,input_next;
    };
    const auto value_count=expressions+facts+caught;
    const auto initialization_count=initialization?initialization->states:0;
    RequireTree(initialization_count<=std::numeric_limits<std::uint32_t>::max()-value_count-events,Error::kResourceLimit);
    const auto event_offset=value_count+initialization_count,count=event_offset+events;
    const auto event_node=[&](FlowPoint point){return NodeId{static_cast<std::uint32_t>(event_offset+point.value)};};
    std::vector<K> values,entries,original_results;
    std::vector<std::optional<std::uint32_t>> heads;
    std::vector<std::uint8_t> queued,changed,original_entries;
    std::vector<ValueOccurrence> occurrences;
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
    budget.Array(values,value_count);budget.Array(heads,count);budget.Array(queued,count);budget.Array(pending,count);
    budget.Array(occurrences,expressions);budget.Array(entries,flow.bindings.size());
    budget.Array(changed,count);budget.Array(original_entries,flow.bindings.size());
    budget.Array(original_results,expressions);
    RequireTree(flow.original_results.empty()||(input.original&&flow.original_results.size()==input.original->operations.size()),Error::kOwnership);
    for(std::size_t i=0;i<flow.original_results.size();++i) {
      budget.Step();const auto& occurrence=flow.original_results[i];if(!occurrence)continue;
      const auto result=FindOriginalResult(*input.original,analysis::OpId{static_cast<std::uint32_t>(i)});
      RequireTree(result&&result->expression==occurrence->expression&&
          occurrence->expression.value<expressions&&occurrence->entry.value<events&&occurrence->normal.value<events,Error::kOwnership);
      auto& kinds=original_results[occurrence->expression.value];
      kinds=kinds.Join(result->kinds);
      provenance_required|=!kinds.IsEmpty();
    }
    // Entry domain is a type proof from the verified original SSA, not an
    // immutable value/cell identity. Mapped arguments/captures remain unknown
    // through the adapter's exposure/stable-entry contract.
    if(input.original)for(const auto& value:input.original->values) {
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
    struct Constant {NodeId target;K value;Guard guard;std::optional<std::uint32_t> next;};
    const auto inputs=[&](const auto& constant,const auto& value_dependency,const auto& proof_invalidation) {
      // Validity is demanded by conditional proof consumers, not by the
      // presence of an OriginalFlow container. With no result/definition caps,
      // this dependency component has no observers. Value, initialization and
      // executable-completion equations still run in the same solver.
      const auto dependency=[&](NodeId source,NodeId target,DependencyKind kind,const FlowDefinition* definition) {
        if(kind!=DependencyKind::kProvenance||provenance_required)value_dependency(source,target,kind,definition);
      };
      const auto invalidate=[&](NodeId target){if(provenance_required)proof_invalidation(target);};
      // Execution context is one shared completion graph, not a backward walk
      // for each definition. Range proofs may depend on prior path predicates.
      for(std::size_t i=0;i<events;++i) {
        budget.Step();const auto& event=flow.events[i];
        const auto target=event_node(FlowPoint{static_cast<std::uint32_t>(i)});
        if(provenance_required&&event.expression)dependency(NodeId{event.expression->value},target,DependencyKind::kProvenance,nullptr);
        if(completion&&event.expression) {
          dependency(NodeId{event.expression->value},target,DependencyKind::kEffects,nullptr);
          VisitExpressionChildren(tree,tree.expressions[event.expression->value].expression,[&](ExprId operand) {
            budget.Step();dependency(NodeId{operand.value},target,DependencyKind::kEffects,nullptr);
          });
        }
        if(provenance_required)for(auto edge=event.incoming;edge;edge=flow.edges[*edge].incoming) {
          budget.Step();guard={completion?std::optional<std::size_t>{*edge}:std::nullopt,false};
          dependency(event_node(flow.edges[*edge].from),target,DependencyKind::kProvenance,nullptr);
          guard={};
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
        dependency(event_node(*definition.catch_entry),target,DependencyKind::kProvenance,nullptr);
        bool input_seen=false;
        for(auto edge=flow.events[definition.catch_entry->value].incoming;edge;edge=flow.edges[*edge].incoming) {
          budget.Step();const auto& incoming=flow.edges[*edge];if(!live.reachable[incoming.from.value])continue;
          input_seen=true;const auto& source=flow.events[incoming.from.value];
          guard={completion?std::optional<std::size_t>{*edge}:std::nullopt,false};
          if(incoming.completion==FlowCompletion::kThrow&&source.thrown_value) {
            RequireTree(source.thrown_value->value<expressions,Error::kOwnership);
            dependency(NodeId{source.thrown_value->value},target,DependencyKind::kValue,nullptr);
          } else {constant(target,K::Language());invalidate(target);}
        }
        guard={};
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
            dependency(NodeId{value.condition.value},target,DependencyKind::kProvenance,nullptr);}
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
        dependency(event_node(occurrence->entry),NodeId{occurrence->expression.value},DependencyKind::kProvenance,nullptr);
      }
      // Numeric updates have one captured input, a throwing conversion and a
      // success-only store. Prefix/postfix differ in value, but both normal
      // results have this numeric category. Never lend their old finite range
      // to the incremented/decremented value or query the cell after coercion.
      for(const auto& definition:flow.definitions) {
        budget.Step();if(definition.kind!=FlowDefinitionKind::kUpdate||!definition.write||!definition.input)continue;
        RequireTree(definition.write->value<expressions&&definition.input->point.value<events&&
            definition.input->binding.value<flow.bindings.size(),Error::kOwnership);
        const NodeId target{definition.write->value};
        occurrences[target.value]=ValueOccurrence::kNumeric;
        dependency(event_node(definition.point),target,DependencyKind::kProvenance,nullptr);
        if(const auto source=fact_node({definition.input->point,definition.input->binding}))
          dependency(*source,target,DependencyKind::kNumeric,nullptr);
        else {constant(target,K::Language());invalidate(target);}
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
        const NodeId target{expression->value};occurrences[expression->value]=ValueOccurrence::kRead;
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
        if(!live.forwarded.empty()&&live.forwarded[i]) {
          // The sparse index proves unchanged transport, not execution. Keep
          // this query's last completion guard so an unexecuted read contributes
          // no value even when its dominating definition already completed.
          const auto source=fact_node(*live.forwarded[i]);RequireTree(source.has_value(),Error::kOwnership);
          for(auto edge=event.incoming;edge;edge=flow.edges[*edge].incoming) {
            budget.Step();if(!live.reachable[flow.edges[*edge].from.value])continue;
            guard={completion?std::optional<std::size_t>{*edge}:std::nullopt,false};
            if(flow.TransferBinding(fact.binding,flow.edges[*edge])!=FlowUnitTransfer::kPreserved){constant(target,K::Language());invalidate(target);}
            else dependency(*source,target,DependencyKind::kValue,nullptr);
          }
          guard={};continue;
        }
        if(fact.point==flow.entry||!event.incoming){constant(target,K::Language());invalidate(target);}
        for(auto edge=event.incoming;edge;edge=flow.edges[*edge].incoming) {
          budget.Step();const auto predecessor=flow.edges[*edge].from;
          if(!live.reachable[predecessor.value])continue;
          guard={completion?std::optional<std::size_t>{*edge}:std::nullopt,false};
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
                case FlowDefinitionKind::kUpdate:
                  if(generated.input)if(const auto source=fact_node({generated.input->point,generated.input->binding})) {
                    dependency(*source,target,DependencyKind::kNumeric,nullptr);
                    dependency(event_node(generated.point),target,DependencyKind::kProvenance,nullptr);
                    // The source action supplies a category, not the raw
                    // result's identity/range (especially for postfix).
                    invalidate(target);continue;
                  }
                  break;
                case FlowDefinitionKind::kAssignment:case FlowDefinitionKind::kInitializer:
                  if(generated.value){RequireTree(generated.value->value<expressions,Error::kOwnership);dependency(NodeId{generated.value->value},target,DependencyKind::kValue,&generated);
                    dependency(event_node(generated.point),target,DependencyKind::kProvenance,nullptr);
                    if(generated.write)dependency(NodeId{generated.write->value},target,DependencyKind::kProvenance,nullptr);
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
                  dependency(event_node(generated.point),target,DependencyKind::kProvenance,nullptr);
                  RequireTree(catch_values[definition->value].has_value(),Error::kOwnership);
                  dependency(*catch_values[definition->value],target,DependencyKind::kValue,&generated);
                  continue;
                case FlowDefinitionKind::kUndefined:case FlowDefinitionKind::kUndefinedStore:
                case FlowDefinitionKind::kFunctionSelf:break;
              }
              constant(target,K::Language());invalidate(target);continue;
            }
          }
          // The actual completion's user effects can replace exposed units.
          // Retained guards, allocation and other declarative writes cannot.
          // Successful stores above independently generate their new value.
          if(flow.bindings[fact.binding.value].MayBeChangedByUserCode()) {
            if(completion) {
              guard.user_code=true;constant(target,K::Language());invalidate(target);guard.user_code=false;
            } else if(prior.MayInvokeUserCode(flow.edges[*edge].completion)) {
              constant(target,K::Language());invalidate(target);continue;
            }
          }
          if(writes[predecessor.value]==fact.binding){constant(target,K::Language());invalidate(target);continue;}
          if(const auto source=fact_node({predecessor,fact.binding}))dependency(*source,target,DependencyKind::kValue,nullptr);
          else {constant(target,K::Language());invalidate(target);}
        }
        guard={};
      }
    };
    std::size_t edges=0,constant_count=0,invalidation_count=0;
    inputs([&](NodeId,K){++constant_count;},[&](NodeId,NodeId,DependencyKind,const FlowDefinition*){++edges;},[&](NodeId){++invalidation_count;});
    RequireTree(edges<=std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
    std::vector<Dependency> dependencies;budget.Array(dependencies,edges);
    std::vector<Constant> constants;budget.Array(constants,constant_count);
    struct Invalidation {NodeId target;Guard guard;std::optional<std::uint32_t> next;};
    std::vector<Invalidation> invalidations;budget.Array(invalidations,invalidation_count);
    std::vector<std::optional<std::uint32_t>> guarded,constant_heads,invalidation_heads,incoming;
    std::vector<std::uint8_t> propagated_invalidation;budget.Array(propagated_invalidation,count);
    budget.Array(incoming,value_count);budget.Array(invalidation_heads,completion?flow.edges.size():0);
    std::vector<std::uint8_t> executable,reached,resolved;
    std::vector<std::size_t> missing;
    std::vector<FlowEventEffects> effects;
    budget.Array(guarded,completion?flow.edges.size():0);budget.Array(constant_heads,completion?flow.edges.size():0);
    budget.Array(executable,completion?flow.edges.size():0);budget.Array(reached,completion?events:0);
    budget.Array(resolved,completion?value_count:0);budget.Array(missing,completion?events:0);budget.Array(effects,completion?events:0);
    std::size_t next=0,constant_next=0,invalidation_next=0,ready=0;
    const auto enqueue=[&](NodeId id){if(!queued[id.value]){queued[id.value]=1;pending[ready++]=id;}};
    const auto invalidate=[&](NodeId id){budget.Step();if(!changed[id.value]){changed[id.value]=1;enqueue(id);}};
    const auto update=[&](NodeId id,K incoming) {
      budget.Step();auto& value=values[id.value];const auto joined=value.Join(incoming);
      if(joined==value)return;
      value=joined;enqueue(id);
    };
    inputs([&](NodeId target,K value) {
      RequireTree(constant_next<std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
      const auto id=static_cast<std::uint32_t>(constant_next++);constants[id]={target,value,guard,{}};
      if(guard.edge){constants[id].next=constant_heads[*guard.edge];constant_heads[*guard.edge]=id;}
    },[&](NodeId source,NodeId target,DependencyKind kind,const FlowDefinition* definition) {
      const auto id=static_cast<std::uint32_t>(next++);
      dependencies[id]={target,heads[source.value],kind,definition,guard,source,{},{}};heads[source.value]=id;
      if(kind==DependencyKind::kValue||kind==DependencyKind::kNumeric) {
        RequireTree(target.value<value_count,Error::kOwnership);
        dependencies[id].input_next=incoming[target.value];incoming[target.value]=id;
      }
      if(guard.edge){dependencies[id].guarded_next=guarded[*guard.edge];guarded[*guard.edge]=id;}
      if(kind==DependencyKind::kEffects)++missing[target.value-event_offset];
    },[&](NodeId target) {
      RequireTree(invalidation_next<std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
      const auto id=static_cast<std::uint32_t>(invalidation_next++);invalidations[id]={target,guard,{}};
      if(guard.edge){invalidations[id].next=invalidation_heads[*guard.edge];invalidation_heads[*guard.edge]=id;}
    });
    // A raw successful-definition domain remains conditional on its entire
    // current operand/completion cone. Source edits, unknown cells and
    // unmodeled environments invalidate once and propagate on the SAME sparse
    // graph; predicates order completion but never contribute their value type.
    // Completion edits are produced by the current/trial adapter, including
    // omitted non-value actions. Reading a plan here duplicated adaptation and
    // could revive raw proofs when a caller retained canonical value equations.
    if(provenance_required) {
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
        if(std::holds_alternative<FlowBindingValue>(ProjectedRecoveryEquation(input,ExprId{static_cast<std::uint32_t>(i)}))&&occurrences[i]==ValueOccurrence::kUnmodeled)changed[i]=1;
      }
      // Value-only graph clients can still solve current source domains. Missing
      // boundary provenance withdraws raw path precision without rejecting them.
      if(!boundaries)for(std::size_t i=0;i<events;++i) {
        budget.Step();changed[event_node(FlowPoint{static_cast<std::uint32_t>(i)}).value]=1;
      }
    }
    for(std::size_t i=0;i<count;++i){budget.Step();if(changed[i])enqueue(NodeId{static_cast<std::uint32_t>(i)});}
    for(const auto& seed:invalidations){budget.Step();if(!seed.guard.edge)invalidate(seed.target);}
    enum class InitializationState : std::uint8_t {kUnreachable=0,kUninitialized=1,kInitialized=2,kMixed=3};
    enum class StoreRetention : std::uint8_t {kObservable,kPrivateAssignment,kInitialization};
    struct InitializationLink {std::optional<std::size_t> source_next,completion_next;};
    std::vector<InitializationState> initialization_states;
    std::vector<std::uint8_t> initialization_resolved,initialized_reads,normal_stores;
    std::vector<FlowReadCompletion> read_completions;
    std::vector<ExpressionEvaluation> assignments;
    std::vector<StoreRetention> store_retention;
    std::vector<InitializationLink> initialization_links;
    std::vector<std::optional<std::size_t>> initialization_heads,initialization_edges,observers,observer_next;
    budget.Array(initialization_states,initialization_count);budget.Array(initialization_resolved,initialization_count);
    budget.Array(initialization_heads,initialization_count);budget.Array(observers,initialization_count);
    budget.Array(initialization_links,initialization?initialization->clauses.size():0);
    budget.Array(initialization_edges,initialization?flow.edges.size():0);
    budget.Array(observer_next,initialization?flow.references.size():0);
    budget.Array(initialized_reads,initialization?expressions:0);budget.Array(normal_stores,initialization?expressions:0);
    budget.Array(assignments,initialization?expressions:0);budget.Array(store_retention,initialization?expressions:0);
    budget.Array(read_completions,initialization?events:0);
    const auto initialization_value=[](FlowInitializationValue value) {
      return value==FlowInitializationValue::kInitialized?InitializationState::kInitialized:InitializationState::kUninitialized;
    };
    const auto initialization_input=[&](const FlowInitializationInput& value) {
      if(const auto* literal=std::get_if<FlowInitializationValue>(&value))return initialization_value(*literal);
      const auto state=std::get<FlowInitializationId>(value).value;
      RequireTree(state<initialization_states.size(),Error::kOwnership);return initialization_states[state];
    };
    const auto update_initialization=[&](FlowInitializationId id,InitializationState value) {
      budget.Step();RequireTree(id.value<initialization_states.size(),Error::kOwnership);
      auto& current=initialization_states[id.value];
      const auto joined=static_cast<InitializationState>(static_cast<std::uint8_t>(current)|static_cast<std::uint8_t>(value));
      if(current!=joined){current=joined;enqueue(NodeId{static_cast<std::uint32_t>(value_count+id.value)});}
    };
    const auto publish_initialization=[&](std::size_t reference_index,InitializationState state) {
      const auto& reference=flow.references[reference_index];const bool initialized=state==InitializationState::kInitialized;
      if(reference.kind==FlowReferenceKind::kRead)
        read_completions[reference.point.value]=initialized?FlowReadCompletion::kInitialized:FlowReadCompletion::kUnproved;
      if(const auto* expression=std::get_if<ExprId>(&reference.site)) {
        const auto id=expression->value;initialized_reads[id]=initialized;
        if(reference.kind==FlowReferenceKind::kWrite) {
          const auto* assignment=std::get_if<AssignmentExpr>(&tree.expressions[id].expression);
          const auto* target=assignment?std::get_if<SymbolTarget>(&assignment->target):nullptr;
          normal_stores[id]=initialized&&target&&WritableRecoveryBinding(tree,target->symbol);
          switch(store_retention[id]) {
            case StoreRetention::kObservable:assignments[id]=ExpressionEvaluation::kRequired;break;
            case StoreRetention::kPrivateAssignment:
              assignments[id]=normal_stores[id]?ExpressionEvaluation::kInert:ExpressionEvaluation::kRequired;break;
            case StoreRetention::kInitialization:assignments[id]=ExpressionEvaluation::kInert;break;
          }
        }
      }
      enqueue(event_node(reference.point));
    };
    if(initialization) {
      std::copy(completion->assignments.begin(),completion->assignments.end(),assignments.begin());
      for(const auto& definition:flow.definitions) {
        budget.Step();if(!definition.write||flow.bindings[definition.binding.value].exposure!=FlowBindingExposure::kPrivate)continue;
        if(definition.kind==FlowDefinitionKind::kInitializer)store_retention[definition.write->value]=StoreRetention::kInitialization;
        else if(definition.kind==FlowDefinitionKind::kAssignment)store_retention[definition.write->value]=StoreRetention::kPrivateAssignment;
      }
      for(std::size_t i=0;i<initialization->clauses.size();++i) {
        budget.Step();const auto& clause=initialization->clauses[i];auto& link=initialization_links[i];
        RequireTree(clause.target.value<initialization_count,Error::kOwnership);
        if(const auto* source=std::get_if<FlowInitializationId>(&clause.input)) {
          RequireTree(source->value<initialization_count,Error::kOwnership);
          link.source_next=initialization_heads[source->value];initialization_heads[source->value]=i;
        }
        if(clause.completion) {
          RequireTree(*clause.completion<flow.edges.size(),Error::kOwnership);
          link.completion_next=initialization_edges[*clause.completion];initialization_edges[*clause.completion]=i;
        } else update_initialization(clause.target,initialization_input(clause.input));
      }
      for(std::size_t i=0;i<flow.references.size();++i) {
        budget.Step();const auto& reference=flow.references[i];const LiveBinding key{reference.point,reference.binding};
        const auto found=std::lower_bound(initialization->queries.begin(),initialization->queries.end(),key,
            [&](const auto& query,const auto& target){budget.Step();return query.fact<target;});
        RequireTree(found!=initialization->queries.end()&&found->fact==key,Error::kOwnership);
        if(const auto* source=std::get_if<FlowInitializationId>(&found->input)) {
          RequireTree(source->value<initialization_count,Error::kOwnership);
          observer_next[i]=observers[source->value];observers[source->value]=i;++missing[reference.point.value];
        } else publish_initialization(i,initialization_input(found->input));
      }
    }
    for(const auto& constant:constants){budget.Step();if(!constant.guard.edge)update(constant.target,constant.value);}
    const auto expression=[&](NodeId target) {
      const auto equation=ProjectedRecoveryEquation(input,ExprId{target.value});
      auto incoming=std::visit([&](const auto& value)->K {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,FlowExpressionValue>)return values[value.expression.value];
        else if constexpr(std::is_same_v<T,FlowChoiceValue>)return values[value.consequent.value].Join(values[value.alternate.value]);
        else if constexpr(std::is_same_v<T,FlowLogicalValue>)return values[value.left.value].Join(values[value.right.value]);
        else if constexpr(std::is_same_v<T,FlowPrimitiveReference>) {
          RequireTree(value.primitive.value<input.bindings.primitives.size(),Error::kOwnership);return PrimitiveKinds(input.bindings.primitives[value.primitive.value]);
        } else if constexpr(std::is_same_v<T,FlowBindingValue>)return occurrences[target.value]==ValueOccurrence::kRead?K{}:K::Language();
        else {static_assert(std::is_same_v<T,UnknownFlowValue>);
          if(occurrences[target.value]==ValueOccurrence::kNumeric)return values[target.value];
          return NormalValueKinds(tree.expressions[target.value].expression,{values.data(),expressions});}
      },equation);
      const auto original=original_results[target.value];
      // Inline results and stored results use the same operand/completion
      // validity. Operand correspondence is not a result-domain certificate.
      if(!changed[target.value]&&!original.IsEmpty())incoming=incoming.Meet(original);
      return incoming;
    };
    for(std::size_t i=0;i<expressions;++i){budget.Step();const NodeId id{static_cast<std::uint32_t>(i)};update(id,expression(id));}
    // Sparse conditional propagation: value nodes only widen, executable
    // completions only activate. An inactive edge contributes no value or
    // external mutation. Each value/operand change wakes indexed dependents;
    // readiness counters avoid rescanning variadic operands per wake-up.
    // Initialization and raw-proof validity share the same edge activation.
    // Withdrawing a raw cap wakes indexed incoming contributions as well as
    // users; an unchanged source value may need to widen its capped target.
    const auto admitted=[&](Guard condition) {
      if(!condition.edge)return true;
      if(!executable[*condition.edge])return false;
      if(!condition.user_code)return true;
      const auto& edge=flow.edges[*condition.edge];const auto& effect=effects[edge.from.value];
      // Reuse the common completion contract, including resume-throw and
      // resume-return; normal/throw alone would omit suspension effects.
      auto event=flow.events[edge.from.value];event.effect=effect.retention;event.effects=effect.intrinsic;
      return event.MayInvokeUserCode(edge.completion);
    };
    const auto propagate=[&](const Dependency& dependency) {
      if(!admitted(dependency.guard))return;
      if(changed[dependency.source.value])invalidate(dependency.target);
      if(dependency.kind==DependencyKind::kProvenance||dependency.kind==DependencyKind::kEffects)return;
      const auto target=dependency.target;
      auto incoming=dependency.kind==DependencyKind::kNumeric?NumericValueKinds(values[dependency.source.value]):values[dependency.source.value];
      if(dependency.definition&&!changed[dependency.source.value]&&!changed[target.value]) {
        K original;
        if(dependency.definition->original) {
          RequireTree(input.original!=nullptr,Error::kOwnership);original=OriginalDefinitionKinds(*input.original,*dependency.definition->original);
        }
        if(const auto* caught=dependency.definition->original_catch) {
          RequireTree(input.original&&caught->value.value<input.original->values.size(),Error::kOwnership);
          original=input.original->values[caught->value.value].domain;
        }
        // Intersect independent proofs, preserving both source precision
        // and monotonicity while incoming contributions are still partial.
        if(!original.IsEmpty())incoming=incoming.Meet(original);
      }
      const bool read=target.value<expressions&&std::holds_alternative<FlowBindingValue>(ProjectedRecoveryEquation(input,ExprId{target.value}));
      update(target,target.value>=expressions||read||dependency.kind==DependencyKind::kNumeric?incoming:expression(target));
    };
    if(completion){reached[flow.entry.value]=1;enqueue(event_node(flow.entry));}
    const auto solve=[&] {
      while(ready) {
        budget.Step();const auto source=pending[--ready];queued[source.value]=0;
        if(changed[source.value]&&!propagated_invalidation[source.value]) {
          propagated_invalidation[source.value]=1;
          if(source.value<expressions)update(source,expression(source));
          if(source.value<value_count)for(auto edge=incoming[source.value];edge;edge=dependencies[*edge].input_next) {
            budget.Step();propagate(dependencies[*edge]);
          }
          for(auto edge=heads[source.value];edge;edge=dependencies[*edge].next){budget.Step();propagate(dependencies[*edge]);}
        }
        if(source.value>=event_offset) {
          if(!completion)continue;
          const auto point=source.value-event_offset;
          if(!reached[point]||missing[point])continue;
          auto facts=*completion;facts.normal_kinds={values.data(),expressions};
          auto event=flow.events[point];
          if(initialization){facts.normal_stores=normal_stores;facts.assignments=assignments;event.read_completion=read_completions[point];}
          effects[point]=EvaluateFlowEventEffects(flow,tree,event,facts);
          for(auto edge=flow.events[point].outgoing;edge;edge=flow.edges[*edge].outgoing) {
            budget.Step();const auto& route=flow.edges[*edge];
            if(route.completion==FlowCompletion::kThrow&&flow.events[point].expression&&effects[point].intrinsic&&
                effects[point].intrinsic->exceptional==analysis::ExceptionalCompletion::kImpossible)continue;
            if(!executable[*edge]) {
              executable[*edge]=1;
              if(initialization)for(auto item=initialization_edges[*edge];item;item=initialization_links[*item].completion_next) {
                budget.Step();const auto& clause=initialization->clauses[*item];update_initialization(clause.target,initialization_input(clause.input));
              }
              if(!reached[route.to.value]){reached[route.to.value]=1;enqueue(event_node(route.to));}
              for(auto item=guarded[*edge];item;item=dependencies[*item].guarded_next){budget.Step();propagate(dependencies[*item]);}
            }
            for(auto item=invalidation_heads[*edge];item;item=invalidations[*item].next) {
              budget.Step();const auto& seed=invalidations[*item];if(admitted(seed.guard))invalidate(seed.target);
            }
            for(auto item=constant_heads[*edge];item;item=constants[*item].next) {
              budget.Step();const auto& constant=constants[*item];if(admitted(constant.guard))update(constant.target,constant.value);
            }
          }
          continue;
        }
        if(source.value>=value_count) {
          const auto state=source.value-value_count;
          const bool first=!initialization_resolved[state]&&initialization_states[state]!=InitializationState::kUnreachable;
          if(first)initialization_resolved[state]=1;
          for(auto item=initialization_heads[state];item;item=initialization_links[*item].source_next) {
            budget.Step();const auto& clause=initialization->clauses[*item];
            if(!clause.completion||executable[*clause.completion])update_initialization(clause.target,initialization_states[state]);
          }
          for(auto item=observers[state];item;item=observer_next[*item]) {
            budget.Step();const auto point=flow.references[*item].point;
            if(first){RequireTree(missing[point.value]>0,Error::kOwnership);--missing[point.value];}
            publish_initialization(*item,initialization_states[state]);
          }
          continue;
        }
        const bool first=completion&&!resolved[source.value]&&!values[source.value].IsEmpty();
        if(first)resolved[source.value]=1;
        for(auto edge=heads[source.value];edge;edge=dependencies[*edge].next) {
          budget.Step();const auto& dependency=dependencies[*edge];
          if(dependency.kind==DependencyKind::kEffects) {
            if(first){auto& count=missing[dependency.target.value-event_offset];RequireTree(count>0,Error::kOwnership);--count;}
            enqueue(dependency.target);
          } else propagate(dependency);
        }
      }
    };
    solve();
    // Ungrounded cycles must not invent a numeric value. The lattice only
    // widens; work is bounded by dependency edges times finite domain height.
    for(std::size_t i=0;i<value_count;++i){budget.Step();if(values[i].IsEmpty())update(NodeId{static_cast<std::uint32_t>(i)},K::Language());}
    solve();
    if(initialization)for(std::size_t i=0;i<flow.references.size();++i) {
      budget.Step();if(!reached[flow.references[i].point.value])publish_initialization(i,InitializationState::kInitialized);
    }
    FlowNormalKinds result;budget.Array(result.expressions,expressions);
    std::copy_n(values.begin(),expressions,result.expressions.begin());
    result.executable=std::move(executable);
    result.initialized_reads=std::move(initialized_reads);result.read_completions=std::move(read_completions);
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(Error error){return error;}
  catch(const std::bad_alloc&){return Error::kResourceLimit;}
  catch(const std::length_error&){return Error::kResourceLimit;}
}
}  // namespace
std::variant<FlowNormalKinds,TreeStructureError> AnalyzeFlowNormalKinds(
    const FunctionTree& tree,const RecoveryFlow& flow,const FlowLiveness& live,
    const RecoveryExpressionInput& input,analysis::SemanticBudget allowance,
    const FlowEffectInput* completion,const FlowInitializationProgram* initialization) {
  return SolveFlowNormalKinds(tree,flow,{live.live_in,live.reachable,{}},input,allowance,completion,initialization);
}
std::variant<FlowNormalKinds,TreeStructureError> AnalyzeFlowNormalKinds(
    const FunctionTree& tree,const RecoveryFlow& flow,const FlowValueStates& states,
    const RecoveryExpressionInput& input,analysis::SemanticBudget allowance,
    const FlowEffectInput* completion,const FlowInitializationProgram* initialization) {
  if(states.owner!=&flow||states.forwarded.size()!=states.states.size())return TreeStructureError::kOwnership;
  return SolveFlowNormalKinds(tree,flow,{states.states,states.reachable,states.forwarded},input,allowance,completion,initialization);
}
}  // namespace abcd::source
