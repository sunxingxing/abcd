#include "flow_copies.h"
#include "tree_checks.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace abcd::source {
std::variant<FlowCopies,TreeStructureError> AnalyzeFlowCopies(
    const RecoveryFlow& flow,const FlowLiveness& live,FlowValueEquations expressions,
    analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);FlowCopies result;
    const auto completed=expressions.completed_reads;
    RequireTree(expressions.size()<=std::numeric_limits<std::uint32_t>::max()&&
        flow.definitions.size()<=std::numeric_limits<std::uint32_t>::max()&&
        flow.edges.size()<=std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
    RequireTree(completed.size()==expressions.size(),TreeStructureError::kOwnership);
    RequireTree(expressions.replacements.empty()||expressions.replacements.size()==expressions.size(),TreeStructureError::kOwnership);
    RequireTree(expressions.bindings.empty()||expressions.bindings.size()==flow.bindings.size(),TreeStructureError::kOwnership);
    RequireTree(expressions.read_values.empty()||expressions.read_values.size()==expressions.size(),TreeStructureError::kOwnership);
    RequireTree(expressions.read_sources.empty()||expressions.read_sources.size()==expressions.size(),TreeStructureError::kOwnership);
    RequireTree(expressions.transports.empty()||expressions.transports.size()==expressions.size(),TreeStructureError::kOwnership);
    RequireTree(expressions.invariants.empty()||expressions.invariants.size()==flow.bindings.size(),TreeStructureError::kOwnership);
    RequireTree(live.reachable.size()==flow.events.size(),TreeStructureError::kOwnership);
    const auto reachable=[&](FlowPoint point) {
      RequireTree(point.value<flow.events.size(),TreeStructureError::kTarget);
      return live.reachable[point.value]!=0;
    };
    for(const auto& binding:flow.bindings){budget.Step();if(binding.stable_entry)
      RequireTree(binding.kind==BindingKind::kParameter||binding.kind==BindingKind::kFunctionSelf,TreeStructureError::kBinding);}
    const auto root_binding=[&](SymbolId id) {
      const auto& binding=flow.bindings[id.value];
      if(binding.stable_entry)return true;
      // This lattice names current observable units; private carriers inherit
      // an available root, rather than becoming immutable values. Scope sites
      // alone cannot supply a unit: the current common transfer model must own it.
      return binding.exposure!=FlowBindingExposure::kPrivate&&
          flow.BindingUnit(id)==FlowUnitCapability::kCurrent;
    };
    std::vector<std::optional<SymbolId>> writes;budget.Array(writes,flow.events.size());
    bool seed=false;
    for(std::size_t i=0;i<flow.definitions.size();++i) {
      budget.Step();const auto& definition=flow.definitions[i];
      RequireTree(definition.binding.value<flow.bindings.size(),TreeStructureError::kBinding);
      RequireTree(definition.point.value<flow.events.size()&&flow.events[definition.point.value].definition==FlowDefinitionId{static_cast<std::uint32_t>(i)},TreeStructureError::kOwnership);
      writes[definition.point.value]=definition.binding;
      if(definition.copy){
        RequireTree(definition.copy->value<flow.bindings.size(),TreeStructureError::kBinding);
        seed|=root_binding(*definition.copy);
      }
    }
    // A typed write reference may accompany the normal definition. Never
    // silently overlook a write at another event, including future adapters.
    for(const auto& reference:flow.references) {
      budget.Step();if(reference.kind!=FlowReferenceKind::kWrite)continue;
      RequireTree(reference.binding.value<flow.bindings.size()&&reference.point.value<writes.size(),TreeStructureError::kOwnership);
      auto& write=writes[reference.point.value];
      RequireTree(!write||*write==reference.binding,TreeStructureError::kOwnership);write=reference.binding;
    }
    if(!seed){result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;}
    const auto read_site=[&](const FlowReference& reference)->std::optional<ExprId> {
      if(reference.kind!=FlowReferenceKind::kRead)return {};
      RequireTree(reference.binding.value<flow.bindings.size(),TreeStructureError::kBinding);
      if(!reachable(reference.point)||flow.bindings[reference.binding.value].exposure!=FlowBindingExposure::kPrivate)return {};
      const auto* expression=std::get_if<ExprId>(&reference.site);if(!expression)return {};
      RequireTree(expression->value<completed.size(),TreeStructureError::kOwnership);
      if(!completed[expression->value])return {};
      const auto equation=expressions[expression->value];
      const auto* read=std::get_if<FlowBindingValue>(&equation);if(!read)return {};
      RequireTree(read->binding==reference.binding,TreeStructureError::kOwnership);
      return *expression;
    };
    std::vector<std::uint8_t> selected;budget.Array(selected,flow.bindings.size());
    for(const auto& reference:flow.references){budget.Step();if(read_site(reference))selected[reference.binding.value]=1;}
    for(const auto& definition:flow.definitions){budget.Step();if(definition.copy&&
        flow.bindings[definition.copy->value].exposure==FlowBindingExposure::kPrivate)selected[definition.copy->value]=1;}
    struct FactId {std::uint32_t value;};
    std::vector<std::uint32_t> facts;std::size_t count=0;std::optional<LiveBinding> previous;
    RequireTree(live.live_in.size()<=std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
    for(const auto& fact:live.live_in) {
      budget.Step();RequireTree(fact.binding.value<selected.size()&&fact.point.value<flow.events.size()&&(!previous||*previous<fact),TreeStructureError::kOwnership);previous=fact;
      count+=selected[fact.binding.value]&&reachable(fact.point);
    }
    budget.Array(facts,count);std::size_t next=0;
    for(std::size_t i=0;i<live.live_in.size();++i){budget.Step();const auto& fact=live.live_in[i];if(selected[fact.binding.value]&&reachable(fact.point))facts[next++]=static_cast<std::uint32_t>(i);}
    const auto find=[&](LiveBinding fact)->std::optional<FactId> {
      const auto found=std::lower_bound(facts.begin(),facts.end(),fact,[&](std::uint32_t id,const LiveBinding& key){budget.Step();return live.live_in[id]<key;});
      if(found==facts.end()||live.live_in[*found]!=fact)return {};
      return FactId{static_cast<std::uint32_t>(found-facts.begin())};
    };
    enum class Kind : std::uint8_t {kUnresolved,kCopy,kUnknown};
    struct Value {Kind kind=Kind::kUnresolved;std::optional<SymbolId> source;};
    struct EdgeIndex {std::uint32_t value;};
    const Value unknown{Kind::kUnknown,{}};
    const auto transfer=[&](Value value,EdgeIndex index,SymbolId target) {
      if(value.kind!=Kind::kCopy)return value;
      const auto root=*value.source;const auto& edge=flow.edges[index.value];
      if(flow.TransferBinding(root,edge)!=FlowUnitTransfer::kPreserved)return unknown;
      const auto& event=flow.events[edge.from.value];
      // A generated copy owns its successful new value; unrelated source
      // writes kill an older relation even when the write itself later dies.
      const bool generated=event.definition&&flow.definitions[event.definition->value].binding==target;
      if(!generated&&writes[edge.from.value]==root)return unknown;
      if(flow.bindings[root.value].MayBeChangedByUserCode()&&
          event.MayInvokeUserCode(edge.completion))return unknown;
      return value;
    };
    const auto generated=[&](const FlowDefinition& definition,const auto& constant,const auto& dependency) {
      switch(definition.kind) {
        case FlowDefinitionKind::kAssignment:case FlowDefinitionKind::kInitializer:
          if(definition.copy) {
            if(root_binding(*definition.copy)){constant(Value{Kind::kCopy,definition.copy});return;}
            if(flow.bindings[definition.copy->value].exposure==FlowBindingExposure::kPrivate)
              if(const auto source=find({definition.point,*definition.copy})){dependency(*source);return;}
          }
          break;
        case FlowDefinitionKind::kUndefined:case FlowDefinitionKind::kUndefinedStore:
        case FlowDefinitionKind::kRestParameter:
        case FlowDefinitionKind::kParameter:
        case FlowDefinitionKind::kClassInner:
        case FlowDefinitionKind::kFunctionSelf:
        case FlowDefinitionKind::kUpdate:case FlowDefinitionKind::kCatch:
        // Current-binding equality cannot certify a captured earlier value:
        // its root might have changed between the capture and commit.
        case FlowDefinitionKind::kIterationCopy:break;
      }
      constant(unknown);
    };
    // Incoming facts describe values before this event. A predecessor's
    // successful definition acts before its outgoing edge; a thrown RHS never
    // traverses that definition. Source writes kill even otherwise dead stores.
    const auto inputs=[&](std::size_t i,const auto& constant,const auto& dependency) {
      const auto& fact=live.live_in[facts[i]];const auto& event=flow.events[fact.point.value];
      if(fact.point==flow.entry||!event.incoming)constant(unknown);
      for(auto edge=event.incoming;edge;edge=flow.edges[*edge].incoming) {
        budget.Step();const auto predecessor=flow.edges[*edge].from;if(!reachable(predecessor))continue;
        if(flow.TransferBinding(fact.binding,flow.edges[*edge])!=FlowUnitTransfer::kPreserved){constant(unknown);continue;}
        const auto defined=flow.events[predecessor.value].definition;
        const EdgeIndex index{static_cast<std::uint32_t>(*edge)};
        if(defined){
          RequireTree(defined->value<flow.definitions.size(),TreeStructureError::kOwnership);
          const auto& definition=flow.definitions[defined->value];
          if(definition.binding==fact.binding){
            generated(definition,[&](Value value){constant(transfer(value,index,fact.binding));},
                [&](FactId source){dependency(source,index);});continue;
          }
        }
        if(writes[predecessor.value]==fact.binding){constant(unknown);continue;}
        if(const auto source=find({predecessor,fact.binding}))dependency(*source,index);
        else constant(unknown);
      }
    };
    struct Dependency {FactId target;EdgeIndex edge;std::optional<std::uint32_t> next;};
    std::vector<Value> values;std::vector<std::uint8_t> queued;
    std::vector<FactId> pending;std::vector<std::optional<std::uint32_t>> heads;
    budget.Array(values,count);budget.Array(queued,count);budget.Array(pending,count);budget.Array(heads,count);
    std::size_t edges=0;
    for(std::size_t i=0;i<count;++i){budget.Step();inputs(i,[](Value){},[&](FactId,EdgeIndex){++edges;});}
    RequireTree(edges<=std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
    std::vector<Dependency> dependencies;budget.Array(dependencies,edges);next=0;std::size_t ready=0;
    const auto join=[&](Value left,Value right) {
      if(left.kind==Kind::kUnknown||right.kind==Kind::kUnresolved)return left;
      if(right.kind==Kind::kUnknown)return unknown;
      if(left.kind==Kind::kUnresolved)return right;
      return left.source==right.source?left:unknown;
    };
    const auto update=[&](FactId target,Value incoming) {
      budget.Step();auto& value=values[target.value];const auto merged=join(value,incoming);
      if(value.kind==merged.kind&&value.source==merged.source)return;
      value=merged;if(!queued[target.value]){queued[target.value]=1;pending[ready++]=target;}
    };
    for(std::size_t i=0;i<count;++i) {
      budget.Step();const FactId target{static_cast<std::uint32_t>(i)};
      inputs(i,[&](Value value){update(target,value);},[&](FactId source,EdgeIndex edge){
        const auto id=static_cast<std::uint32_t>(next++);dependencies[id]={target,edge,heads[source.value]};heads[source.value]=id;
      });
    }
    const auto solve=[&] {
      while(ready) {
        budget.Step();const auto source=pending[--ready];queued[source.value]=0;
        for(auto edge=heads[source.value];edge;edge=dependencies[*edge].next){
          budget.Step();const auto& dependency=dependencies[*edge];const auto value=values[source.value];
          const auto binding=live.live_in[facts[dependency.target.value]].binding;
          update(dependency.target,transfer(value,dependency.edge,binding));
        }
      }
    };
    solve();
    for(std::size_t i=0;i<count;++i){budget.Step();if(values[i].kind==Kind::kUnresolved)update({static_cast<std::uint32_t>(i)},unknown);}
    solve();
    std::vector<Value> reads;budget.Array(reads,completed.size());
    for(const auto& reference:flow.references){budget.Step();if(const auto expression=read_site(reference)){
      const auto fact=find({reference.point,reference.binding});
      reads[expression->value]=join(reads[expression->value],fact?values[fact->value]:unknown);
    }}
    std::size_t output=0;for(const auto& read:reads){budget.Step();output+=read.kind==Kind::kCopy;}
    budget.Array(result.reads,output);next=0;
    for(std::size_t i=0;i<reads.size();++i){budget.Step();if(reads[i].kind==Kind::kCopy)result.reads[next++]={{static_cast<std::uint32_t>(i)},*reads[i].source};}
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
