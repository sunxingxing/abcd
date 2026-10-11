#include "flow_units.h"
#include "flow_cycles.h"
#include "flow_dominance.h"
#include "recovery_scratch.h"
#include <map>
#include <set>
#include <tuple>
namespace abcd::source {
std::variant<FlowUnitRelations,TreeStructureError> AnalyzeFlowUnits(
    const RecoveryFlow& flow,std::span<const FlowUnitQuery> queries,analysis::SemanticBudget allowance) {
  using Error=TreeStructureError;
  try {
    TreeCheckBudget budget(allowance);RecoveryScratch memory(budget);FlowUnitRelations result;
    budget.Array(result.queries,queries.size());
    if(queries.empty())return result;
    using Key=std::tuple<ScopeId,std::optional<FlowPoint>,FlowBindingLifetime,std::optional<FlowPoint>>;
    const auto less=[&](const auto& a,const auto& b){budget.Step();return a<b;};
    std::pmr::map<Key,std::size_t,decltype(less)> index(less,&memory);
    struct Group {FlowUnitQuery query;FlowUnitRelation relation=FlowUnitRelation::kEquivalent;std::optional<FlowComponentId> first;};
    std::pmr::vector<Group> groups(&memory);
    std::pmr::vector<std::optional<std::size_t>> selected(&memory);
    std::pmr::set<std::pair<SymbolId,std::size_t>,decltype(less)> requests(less,&memory);
    std::pmr::vector<std::optional<std::size_t>> heads(&memory),next(&memory);
    budget.Step(queries.size()+flow.bindings.size());selected.resize(queries.size());next.resize(queries.size());heads.resize(flow.bindings.size());
    for(std::size_t i=0;i<queries.size();++i) {
      budget.Step();const auto query=queries[i];
      RequireTree(query.current.value<flow.bindings.size(),Error::kBinding);
      const auto& binding=flow.bindings[query.current.value];result.queries[i]=FlowUnitRelation::kUnmodeled;
      if(!flow.scopes||query.original.value>=flow.scopes->regions.size()||
          flow.BindingUnit(query.current)!=FlowUnitCapability::kCurrent)continue;
      if(const auto* scope=std::get_if<ScopeBindingLifetime>(&binding.lifetime);scope&&scope->scope==query.original&&!query.reset) {
        // Equal reset functions prove identity for all traces, including
        // captures. An extra allocation point must be an existing scope reset.
        const auto point=binding.allocation;
        if(!point||(point->value<flow.events.size()&&flow.events[point->value].scope==query.original&&
            flow.events[point->value].ResetsScopeUnit())) {
          result.queries[i]=FlowUnitRelation::kEquivalent;continue;
        }
      }
      if(binding.MayBeChangedByUserCode()&&!flow.unit_captures_complete)continue;
      const Key key{query.original,query.reset,binding.lifetime,binding.allocation};
      const auto [at,inserted]=index.emplace(key,groups.size());
      if(inserted)groups.push_back({query});selected[i]=at->second;
      if(requests.emplace(query.current,at->second).second){next[i]=heads[query.current.value];heads[query.current.value]=i;}
    }
    if(!groups.empty()) {
      std::optional<FlowDominance> dominance;
      if(std::any_of(groups.begin(),groups.end(),[](const auto& group){return group.query.reset.has_value();})) {
        auto analyzed=AnalyzeFlowDominance(flow,budget.Remaining());
        if(const auto* error=std::get_if<Error>(&analyzed))throw *error;
        dominance=std::get<FlowDominance>(std::move(analyzed));
        budget.Admit(dominance->allocated_bytes,1);budget.Step(dominance->work);
      }
      auto analyzed=AnalyzeFlowCycles(flow,budget.Remaining());
      if(const auto* error=std::get_if<Error>(&analyzed))throw *error;
      const auto& cycles=std::get<FlowCycles>(analyzed);
      budget.Admit(cycles.allocated_bytes,1);budget.Step(cycles.work);
      std::pmr::vector<std::uint8_t> reached(&memory);std::pmr::vector<FlowPoint> visits(&memory);
      budget.Step(flow.events.size());reached.resize(flow.events.size());
      RequireTree(flow.entry.value<flow.events.size(),Error::kTarget);
      reached[flow.entry.value]=1;visits.push_back(flow.entry);
      while(!visits.empty()) {
        budget.Step();const auto point=visits.back();visits.pop_back();
        for(auto e=flow.events[point.value].outgoing;e;e=flow.edges[*e].outgoing) {
          budget.Step();const auto to=flow.edges[*e].to;RequireTree(to.value<reached.size(),Error::kTarget);
          if(!reached[to.value]){reached[to.value]=1;visits.push_back(to);}
        }
      }
      using Observation=std::pair<FlowPoint,std::size_t>;
      // Condensation order stabilizes acyclic indexing under arena changes;
      // it is an index only, never an execution-order admission condition.
      const auto observation_less=[&](const Observation& a,const Observation& b) {
        budget.Step();return std::tuple{cycles.components[a.first.value],a.first,a.second}<
            std::tuple{cycles.components[b.first.value],b.first,b.second};
      };
      std::pmr::set<Observation,decltype(observation_less)> observations(observation_less,&memory);
      const auto observe=[&](FlowPoint point,SymbolId binding) {
        RequireTree(point.value<reached.size()&&binding.value<heads.size(),Error::kOwnership);
        if(!reached[point.value])return;
        for(auto q=heads[binding.value];q;q=next[*q]) {
          budget.Step();const auto group=*selected[*q];const auto original=groups[group].query.original;
          const auto old=flow.scopes->Presence(original,point,FlowScopePhase::kBefore);
          if(const auto reset=groups[group].query.reset;reset&&
              (reset->value>=flow.events.size()||*reset==point||!dominance->Dominates(*reset,point)))
            groups[group].relation=FlowUnitRelation::kUnmodeled;
          const auto current=flow.BindingPresence(binding,point,FlowScopePhase::kBefore);
          if(old!=FlowUnitPresence::kActive||current!=FlowUnitPresence::kActive)
            groups[group].relation=FlowUnitRelation::kUnmodeled;
          observations.emplace(point,group);
          const auto component=cycles.components[point.value];
          if(!groups[group].first||component<*groups[group].first)groups[group].first=component;
        }
      };
      for(const auto& reference:flow.references){budget.Step();observe(reference.point,reference.binding);}
      for(const auto& definition:flow.definitions){budget.Step();observe(definition.point,definition.binding);}
      for(const auto& capture:flow.unit_captures){budget.Step();observe(capture.point,capture.binding);}
      struct Fact {FlowPoint point;std::size_t group;bool original=false,current=false;auto operator<=>(const Fact&)const=default;};
      const auto fact_less=[&](const Fact& a,const Fact& b) {
        budget.Step();return std::tuple{cycles.components[a.point.value],a.point,a.group,a.original,a.current}<
            std::tuple{cycles.components[b.point.value],b.point,b.group,b.original,b.current};
      };
      std::pmr::set<Fact,decltype(fact_less)> facts(fact_less,&memory);std::pmr::vector<Fact> pending(&memory);
      const auto add=[&](Fact fact){if(facts.insert(fact).second)pending.push_back(fact);};
      for(const auto& [point,group]:observations){budget.Step();add({point,group});}
      while(!pending.empty()) {
        budget.Step();const auto fact=pending.back();pending.pop_back();auto& group=groups[fact.group];
        if(group.relation!=FlowUnitRelation::kEquivalent)continue;
        for(auto e=flow.events[fact.point.value].incoming;e;e=flow.edges[*e].incoming) {
          budget.Step();const auto& edge=flow.edges[*e];if(!reached[edge.from.value])continue;
          // No prior observation can reach a strictly earlier SCC. This
          // shared index cuts first-use prefixes without one whole-graph
          // search per binding; cycles keep their full sparse reset facts.
          if(cycles.components[edge.from.value]<*group.first)continue;
          const auto& event=flow.events[edge.from.value];
          const auto old=group.query.reset?
              (edge.from==*group.query.reset?FlowUnitTransfer::kEnded:FlowUnitTransfer::kPreserved):
              (event.scope==group.query.original&&event.ResetsScopeUnit()?FlowUnitTransfer::kEnded:
              flow.scopes->Transfer(group.query.original,edge.from,edge.to));
          const auto current=flow.TransferBinding(group.query.current,edge);
          if(old==FlowUnitTransfer::kUnmodeled||current==FlowUnitTransfer::kUnmodeled) {
            group.relation=FlowUnitRelation::kUnmodeled;break;
          }
          const bool a=fact.original||old==FlowUnitTransfer::kEnded,b=fact.current||current==FlowUnitTransfer::kEnded;
          // Both have become fresh since any earlier observation. Extra empty
          // activations are unobservable for closed units, so this is a cut.
          if(a&&b)continue;
          if(observations.contains({edge.from,fact.group})) {
            if(a!=b){group.relation=FlowUnitRelation::kDifferent;break;}
          } else {
            // Entry can have incoming backedges in a general checked graph.
            // The visited state set, not an entry sentinel, ends the search.
            add({edge.from,fact.group,a,b});
          }
        }
      }
      for(std::size_t i=0;i<queries.size();++i){budget.Step();if(selected[i])result.queries[i]=groups[*selected[i]].relation;}
    }
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  } catch(Error error){return error;}catch(const std::bad_alloc&){return Error::kResourceLimit;}
  catch(const std::length_error&){return Error::kResourceLimit;}
}
}
