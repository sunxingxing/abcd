#include "flow_dominance.h"
#include "flow_initialization_cuts.h"
#include "tree_checks.h"
#include "recovery_scratch.h"
#include <set>
#include <algorithm>
#include <tuple>
#include <limits>
#include <stdexcept>
namespace abcd::source {
namespace {
enum class InitializationOutput {kProofs,kEquations};
std::variant<FlowInitializations,TreeStructureError> PrepareInitializations(
    const RecoveryFlow& flow,std::span<const LiveBinding> queries,analysis::SemanticBudget allowance,
    InitializationOutput output,FlowInitializationProgram* equations) {
  try {
    TreeCheckBudget budget(allowance);RecoveryScratch memory(budget);FlowInitializations result;
    const auto less=[&](const LiveBinding& a,const LiveBinding& b){budget.Step();return a<b;};
    std::pmr::set<LiveBinding,decltype(less)> generated(less,&memory),facts(less,&memory),roots(less,&memory);
    std::pmr::vector<LiveBinding> pending(&memory);
    budget.Array(result.declarations,flow.bindings.size());
    for(const auto& definition:flow.definitions) {
      budget.Step();bool selected=false;
      switch(definition.kind) {
        case FlowDefinitionKind::kRestParameter:case FlowDefinitionKind::kParameter: case FlowDefinitionKind::kCatch:
        case FlowDefinitionKind::kClassInner:
        case FlowDefinitionKind::kFunctionSelf:
          selected = true;
          break;
        case FlowDefinitionKind::kUndefined: case FlowDefinitionKind::kInitializer:
          selected=definition.declaration && definition.declaration==flow.bindings[definition.binding.value].declaration;break;
        case FlowDefinitionKind::kAssignment: case FlowDefinitionKind::kUpdate:
        case FlowDefinitionKind::kUndefinedStore:break;
        case FlowDefinitionKind::kIterationCopy:generated.insert({definition.point,definition.binding});break;
      }
      if(selected){result.declarations[definition.binding.value]=definition.point;generated.insert({definition.point,definition.binding});}
    }
    auto analyzed=AnalyzeFlowDominance(flow,budget.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
    result.order=std::get<FlowDominance>(std::move(analyzed));
    budget.Admit(result.order.allocated_bytes,1);budget.Step(result.order.work);
    // Declaration anchors describe source placement only. The reset-aware
    // must analysis below owns completion; one old declaration's dominance
    // never proves initialization in a later activation or dynamic cell equality.
    for(std::size_t i=0;i<flow.bindings.size();++i) {
      budget.Step();const auto* unit=std::get_if<ScopeBindingLifetime>(&flow.bindings[i].lifetime);
      const auto allocation=flow.bindings[i].allocation?flow.bindings[i].allocation:
          unit?std::optional{unit->enter}:std::nullopt;
      if(flow.BindingUnit(SymbolId{static_cast<std::uint32_t>(i)})==FlowUnitCapability::kUnmodeled||
          (allocation&&result.declarations[i]&&!result.order.Dominates(*allocation,*result.declarations[i])))
        result.declarations[i].reset();
    }
    // Bottom fact: without a reachable initialization, no assignment or old
    // dominance anchor can complete this binding. Index once, avoiding sparse
    // backward walks for every early read of declarations after termination.
    std::pmr::vector<std::uint8_t> generated_reachable(&memory);budget.Step(flow.bindings.size());generated_reachable.resize(flow.bindings.size());
    for(const auto fact:generated){budget.Step();if(result.order.tree.nodes[fact.point.value].begin)generated_reachable[fact.binding.value]=1;}
    auto proof=FindFlowInitializationCertificates(flow,result.order,result.declarations,budget.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&proof))throw *error;
    const auto& certified=std::get<FlowInitializationCertificates>(proof);
    budget.Admit(certified.allocated_bytes,1);budget.Step(certified.work);
    // Classical basic blocks contract straight-line control, never recovery
    // admission. Unit-context changes remain boundaries. Indexed local gen/kill
    // avoids B * instruction walks through implicit multi-binding copy clauses.
    struct BlockId {std::uint32_t value;auto operator<=>(const BlockId&)const=default;};
    struct Location {std::optional<BlockId> block;std::uint32_t offset=0;};
    struct Block {FlowPoint first,last;};
    std::pmr::vector<Location> locations(&memory);
    std::pmr::vector<Block> blocks(&memory);
    std::pmr::vector<std::uint32_t> incoming(&memory),outgoing(&memory);
    const auto size=flow.events.size();budget.Step(size);locations.resize(size);incoming.resize(size);outgoing.resize(size);
    const auto reached=[&](FlowPoint point){return result.order.tree.nodes[point.value].begin!=0;};
    for(const auto& edge:flow.edges){budget.Step();if(reached(edge.from)&&reached(edge.to)){++incoming[edge.to.value];++outgoing[edge.from.value];}}
    const auto transition=[&](FlowPoint point) {
      if(!flow.scopes)return false;
      if(point.value<flow.scopes->events.size()&&flow.scopes->events[point.value].before!=flow.scopes->events[point.value].after)return true;
      return point.value<flow.scopes->loop_events.size()&&flow.scopes->loop_events[point.value].before!=flow.scopes->loop_events[point.value].after;
    };
    const auto linear=[&](FlowPoint from,FlowPoint to) {
      if(outgoing[from.value]!=1||incoming[to.value]!=1||to==flow.entry||transition(from)||transition(to))return false;
      if(flow.scopes) {
        if(from.value<flow.scopes->events.size()&&to.value<flow.scopes->events.size()&&flow.scopes->events[from.value].after!=flow.scopes->events[to.value].before)return false;
        if(from.value<flow.scopes->loop_events.size()&&to.value<flow.scopes->loop_events.size()&&flow.scopes->loop_events[from.value].after!=flow.scopes->loop_events[to.value].before)return false;
      }
      return true;
    };
    for(std::size_t i=0;i<size;++i) {
      budget.Step();const FlowPoint first{static_cast<std::uint32_t>(i)};if(!reached(first))continue;
      bool start=first==flow.entry||incoming[i]!=1;
      if(!start)for(auto edge=flow.events[i].incoming;edge;edge=flow.edges[*edge].incoming){budget.Step();const auto from=flow.edges[*edge].from;if(reached(from)){start=!linear(from,first);break;}}
      if(!start)continue;
      const BlockId block{static_cast<std::uint32_t>(blocks.size())};blocks.push_back({first,first});auto point=first;std::uint32_t offset=0;
      while(true) {
        budget.Step();RequireTree(!locations[point.value].block,TreeStructureError::kOwnership);
        locations[point.value]={block,offset++};blocks.back().last=point;
        std::optional<FlowPoint> next;
        for(auto edge=flow.events[point.value].outgoing;edge;edge=flow.edges[*edge].outgoing){budget.Step();const auto target=flow.edges[*edge].to;if(reached(target)&&linear(point,target)){next=target;break;}}
        if(!next)break;point=*next;
      }
    }
    using BindingAction=std::tuple<SymbolId,BlockId,std::uint32_t>;
    using ScopeAction=std::tuple<ScopeId,BlockId,std::uint32_t>;
    using LoopAction=std::tuple<StmtId,BlockId,std::uint32_t>;
    std::pmr::vector<BindingAction> initializations(&memory);
    std::pmr::vector<ScopeAction> scope_resets(&memory);
    std::pmr::vector<LoopAction> loop_resets(&memory);
    for(const auto fact:generated){budget.Step();if(reached(fact.point)){const auto at=locations[fact.point.value];RequireTree(at.block.has_value(),TreeStructureError::kOwnership);initializations.emplace_back(fact.binding,*at.block,at.offset);}}
    for(std::size_t i=0;i<size;++i) {
      budget.Step();const FlowPoint point{static_cast<std::uint32_t>(i)};if(!reached(point))continue;
      const auto at=locations[i];RequireTree(at.block.has_value(),TreeStructureError::kOwnership);const auto& event=flow.events[i];
      if(event.scope&&event.ResetsScopeUnit())scope_resets.emplace_back(*event.scope,*at.block,at.offset);
      if(event.loop_environment&&event.ResetsLoopUnit())loop_resets.emplace_back(*event.loop_environment,*at.block,at.offset);
    }
    const auto sorted=[&](auto& actions){std::sort(actions.begin(),actions.end(),[&](const auto& a,const auto& b){budget.Step();return a<b;});};
    sorted(initializations);sorted(scope_resets);sorted(loop_resets);
    const auto last=[&](const auto& actions,auto unit,Location at)->std::optional<std::uint32_t> {
      const auto key=std::tuple{unit,*at.block,at.offset};
      auto found=std::upper_bound(actions.begin(),actions.end(),key,[&](const auto& a,const auto& b){budget.Step();return a<b;});
      if(found==actions.begin())return {};--found;
      if(std::get<0>(*found)!=unit||std::get<1>(*found)!=*at.block)return {};return std::get<2>(*found);
    };
    const auto local=[&](LiveBinding fact,const auto& constant,const auto& dependency) {
      if(!reached(fact.point)){constant(true);return;}
      if(!generated_reachable[fact.binding.value]){constant(false);return;}
      const auto& binding=flow.bindings[fact.binding.value];const auto at=locations[fact.point.value];
      const auto presence=flow.BindingPresence(fact.binding,fact.point,FlowScopePhase::kAfter);
      if(presence!=FlowUnitPresence::kActive&&presence!=FlowUnitPresence::kCompletionOnly){constant(false);return;}
      std::optional<std::uint32_t> reset;
      if(const auto allocation=binding.allocation;allocation&&reached(*allocation)) {
        const auto site=locations[allocation->value];if(site.block==at.block&&site.offset<=at.offset)reset=site.offset;
      }
      const auto update_reset=[&](std::optional<std::uint32_t> site){if(site&&(!reset||*reset<*site))reset=site;};
      if(const auto* unit=std::get_if<ScopeBindingLifetime>(&binding.lifetime))
        update_reset(last(scope_resets,unit->scope,at));
      if(const auto* unit=std::get_if<LoopBindingLifetime>(&binding.lifetime))
        update_reset(last(loop_resets,unit->loop,at));
      const auto initialized=last(initializations,fact.binding,at);
      if(initialized&&(!reset||*initialized>=*reset)){constant(true);return;}
      if(reset){constant(false);return;}
      if(certified.clear[fact.binding.value]&&result.order.Dominates(*result.declarations[fact.binding.value],fact.point)){constant(true);return;}
      dependency(LiveBinding{blocks[at.block->value].first,fact.binding});
    };
    const auto add=[&](LiveBinding fact){if(facts.insert(fact).second)pending.push_back(fact);};
    const auto root=[&](LiveBinding fact) {
      RequireTree(fact.point.value<size&&fact.binding.value<flow.bindings.size(),TreeStructureError::kOwnership);
      if(roots.insert(fact).second)local(fact,[](bool){},add);
    };
    for(const auto& reference:flow.references){budget.Step();root({reference.point,reference.binding});}
    for(std::size_t i=0;i<size;++i){budget.Step();if(flow.events[i].read)root({FlowPoint{static_cast<std::uint32_t>(i)},*flow.events[i].read});}
    for(const auto& definition:flow.definitions){budget.Step();root({definition.point,definition.binding});if(definition.input)root({definition.input->point,definition.input->binding});}
    for(const auto fact:queries){budget.Step();root(fact);}
    // Block-entry states form an intersection lattice. Starting at true,
    // entry/reset/unknown transfer counterexamples propagate once. A reachable
    // ungrounded cycle always has such an incoming source; gen cuts are explicit.
    std::optional<std::size_t> input_edge;
    const auto inputs=[&](LiveBinding fact,const auto& constant,const auto& dependency) {
      input_edge.reset();
      if(fact.point==flow.entry){constant(false);return;}
      bool predecessor=false;
      for(auto edge=flow.events[fact.point.value].incoming;edge;edge=flow.edges[*edge].incoming) {
        budget.Step();const auto& incoming=flow.edges[*edge];if(!reached(incoming.from))continue;predecessor=true;input_edge=*edge;
        if(flow.TransferBinding(fact.binding,incoming)!=FlowUnitTransfer::kPreserved)constant(false);
        else local({incoming.from,fact.binding},constant,dependency);
      }
      input_edge.reset();if(!predecessor)constant(false);
    };
    while(!pending.empty()){budget.Step();const auto fact=pending.back();pending.pop_back();inputs(fact,[](bool){},add);}
    std::pmr::vector<LiveBinding> states(&memory);budget.Step(facts.size());states.assign(facts.begin(),facts.end());
    RequireTree(states.size()<=std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
    const auto find=[&](LiveBinding fact) {
      const auto position=std::lower_bound(states.begin(),states.end(),fact,less);
      RequireTree(position!=states.end()&&*position==fact,TreeStructureError::kOwnership);
      return static_cast<std::uint32_t>(position-states.begin());
    };
    FlowInitializationProgram program;program.owner=&flow;program.events=flow.events.size();
    program.completions=flow.edges.size();program.states=states.size();
    std::size_t clauses=0;
    for(const auto state:states){budget.Step();inputs(state,[&](bool){++clauses;},[&](LiveBinding){++clauses;});}
    budget.Array(program.clauses,clauses);std::size_t next=0;
    const auto literal=[](bool initialized){return initialized?FlowInitializationValue::kInitialized:FlowInitializationValue::kUninitialized;};
    for(std::size_t i=0;i<states.size();++i) {
      budget.Step();const FlowInitializationId target{static_cast<std::uint32_t>(i)};
      inputs(states[i],[&](bool value){program.clauses[next++]={target,literal(value),input_edge};},
          [&](LiveBinding source){program.clauses[next++]={target,FlowInitializationId{find(source)},input_edge};});
    }
    budget.Array(program.queries,roots.size());next=0;
    for(const auto fact:roots) {
      budget.Step();FlowInitializationInput input;
      local(fact,[&](bool value){input=literal(value);},[&](LiveBinding source){input=FlowInitializationId{find(source)};});
      program.queries[next++]={fact,input};
    }
    program.retained_bytes=program.clauses.capacity()*sizeof(FlowInitializationClause)+program.queries.capacity()*sizeof(FlowInitializationQuery);
    if(output==InitializationOutput::kEquations) {
      RequireTree(equations!=nullptr,TreeStructureError::kOwnership);
      program.allocated_bytes=budget.Bytes();program.work=budget.Work();*equations=std::move(program);
      return result;
    }
    struct Dependency {std::uint32_t target;std::optional<std::uint32_t> next;};
    std::pmr::vector<Dependency> dependencies(&memory);
    std::pmr::vector<std::optional<std::uint32_t>> heads(&memory);
    std::pmr::vector<std::uint8_t> failed(&memory);
    std::pmr::vector<std::uint32_t> rejected(&memory);
    budget.Step(states.size());heads.resize(states.size());failed.resize(states.size());
    const auto reject=[&](std::uint32_t id){if(!failed[id]){failed[id]=1;rejected.push_back(id);}};
    for(const auto& clause:program.clauses) {
      budget.Step();const auto target=clause.target.value;
      if(const auto* value=std::get_if<FlowInitializationValue>(&clause.input)) {
        if(*value==FlowInitializationValue::kUninitialized)reject(target);
      } else {
        RequireTree(dependencies.size()<std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
        const auto id=std::get<FlowInitializationId>(clause.input).value;
        const auto edge=static_cast<std::uint32_t>(dependencies.size());dependencies.push_back({target,heads[id]});heads[id]=edge;
      }
    }
    while(!rejected.empty()) {
      budget.Step();const auto source=rejected.back();rejected.pop_back();
      for(auto edge=heads[source];edge;edge=dependencies[*edge].next){budget.Step();reject(dependencies[*edge].target);}
    }
    budget.Array(result.completed,roots.size());std::size_t completed=0;
    for(const auto& query:program.queries) {
      budget.Step();const bool initialized=std::holds_alternative<FlowInitializationValue>(query.input)?
          std::get<FlowInitializationValue>(query.input)==FlowInitializationValue::kInitialized:
          !failed[std::get<FlowInitializationId>(query.input).value];
      if(initialized)result.completed[completed++]=query.fact;
    }
    result.completed.resize(completed);
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  } catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}  // namespace
std::variant<FlowInitializations,TreeStructureError> AnalyzeFlowInitializations(
    const RecoveryFlow& flow,analysis::SemanticBudget allowance) {
  return PrepareInitializations(flow,{},allowance,InitializationOutput::kProofs,nullptr);
}
std::variant<FlowInitializations,TreeStructureError> AnalyzeFlowInitializations(
    const RecoveryFlow& flow,std::span<const LiveBinding> queries,analysis::SemanticBudget allowance) {
  return PrepareInitializations(flow,queries,allowance,InitializationOutput::kProofs,nullptr);
}
std::variant<FlowInitializationProgram,TreeStructureError> BuildFlowInitializationProgram(
    const RecoveryFlow& flow,analysis::SemanticBudget allowance) {
  FlowInitializationProgram result;
  auto prepared=PrepareInitializations(flow,{},allowance,InitializationOutput::kEquations,&result);
  if(const auto* error=std::get_if<TreeStructureError>(&prepared))return *error;
  return result;
}
}  // namespace abcd::source
