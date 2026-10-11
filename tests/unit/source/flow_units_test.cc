#include "../../../src/source/flow_units.h"
#include "../../../src/source/flow_cycles.h"
#include <array>
#include <iostream>
#include <set>
#include <source_location>
#include <tuple>
using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool yes,std::source_location at=std::source_location::current()) {
  if(!yes){std::cerr<<"unit relation check "<<at.line()<<'\n';std::abort();}
}
void Link(RecoveryFlow& flow,unsigned from,unsigned to) {
  flow.edges.push_back({{from},{to},FlowCompletion::kNormal,flow.events[to].incoming,flow.events[from].outgoing});
  flow.events[from].outgoing=flow.events[to].incoming=flow.edges.size()-1;
}
RecoveryFlow Graph(unsigned points,unsigned bindings=1) {
  RecoveryFlow flow{};flow.events.resize(points);flow.entry={0};flow.terminal={points-1};
  FunctionTree tree{};tree.scopes={{ScopeKind::kBlock,{}},{ScopeKind::kBlock,ScopeId{0}}};
  TreeCheckBudget budget({});auto scopes=MakeFlowScopes(tree,budget);
  scopes->events.resize(points,{ScopeId{1},ScopeId{1}});flow.scopes=scopes;
  flow.bindings.resize(bindings);
  for(auto& binding:flow.bindings) {
    binding.kind=BindingKind::kLocal;binding.exposure=FlowBindingExposure::kPrivate;
    binding.cell_access=CellVisibility::kClosed;binding.lifetime=ScopeBindingLifetime{{1},{0},{points-1}};
  }
  return flow;
}
RecoveryFlow Split(RecoveryFlow flow) {
  const auto edges=flow.edges;flow.edges.clear();
  auto scopes=std::make_shared<FlowScopes>(*flow.scopes);flow.scopes=scopes;
  for(auto& event:flow.events){event.incoming.reset();event.outgoing.reset();}
  for(const auto& edge:edges) {
    const auto middle=static_cast<unsigned>(flow.events.size());flow.events.push_back({});
    scopes->events.push_back({ScopeId{1},ScopeId{1}});
    Link(flow,edge.from.value,middle);Link(flow,middle,edge.to.value);
  }
  return flow;
}
// Independent forward trace oracle. Counters count dynamic allocations, and
// observations compare their equality to the previous observed token. Normalize
// counters after each observation: only equality to the last token matters.
// No production SCC/transfer/presence or backward solver is reused.
bool Oracle(const std::array<unsigned,3>& actions,unsigned mask,unsigned reads) {
  using State=std::tuple<unsigned,bool,unsigned,unsigned>;
  std::set<State> seen;std::vector<State> todo{{0,false,0,0}};
  while(!todo.empty()) {
    const auto state=todo.back();todo.pop_back();if(!seen.insert(state).second)continue;
    auto [point,observed,old,current]=state;
    if(reads&(1U<<point)) {
      if(observed&&((old==0)!=(current==0)))return false;
      old=current=0;observed=true;
    }
    if(actions[point]==1)old=1;
    if(actions[point]==2)current=1;
    for(unsigned to=0;to<3;++to)if(mask&(1U<<(point*3+to)))todo.emplace_back(to,observed,old,current);
  }
  return true;
}
void Exhaustive() {
  unsigned count=0;
  for(unsigned encoding=0;encoding<27;++encoding)for(unsigned mask=0;mask<512;++mask)for(unsigned reads=1;reads<8;++reads) {
    std::array<unsigned,3> actions{};auto digits=encoding;
    for(auto& action:actions){action=digits%3;digits/=3;}
    const bool expected=Oracle(actions,mask,reads);
    for(bool reverse:{false,true}) {
      auto flow=Graph(3);const auto point=[&](unsigned i){return reverse?2-i:i;};flow.entry={point(0)};
      for(unsigned i=0;i<3;++i) {
        if(actions[i]) {flow.events[point(i)].kind=FlowEventKind::kEnterScope;flow.events[point(i)].scope=ScopeId{actions[i]-1};}
        if(reads&(1U<<i))flow.references.push_back({{point(i)},{0},FlowReferenceKind::kRead,ExprId{i}});
      }
      for(unsigned i=0;i<9;++i)if(mask&(1U<<i))Link(flow,point(i/3),point(i%3));
      const std::array queries{FlowUnitQuery{{0},{0}}};
      auto analyzed=AnalyzeFlowUnits(flow,queries);Check(std::holds_alternative<FlowUnitRelations>(analyzed));
      const auto relation=std::get<FlowUnitRelations>(analyzed).queries[0];
      if((relation==FlowUnitRelation::kEquivalent)!=expected)std::cerr<<encoding<<' '<<mask<<' '<<reads<<' '<<reverse<<'\n';
      Check((relation==FlowUnitRelation::kEquivalent)==expected);++count;
      if(mask%17==0&&reads==3) {
        const auto split=std::get<FlowUnitRelations>(AnalyzeFlowUnits(Split(flow),queries));
        Check(split.queries[0]==relation);
      }
    }
  }
  std::cout<<"unit oracle layouts="<<count<<'\n';
}
void SuccessfulAllocation() {
  auto flow=Graph(6);for(unsigned i=0;i<5;++i)Link(flow,i,i+1);
  flow.events[0].kind=FlowEventKind::kEnterScope;flow.events[0].scope=ScopeId{1};
  flow.references={{{3},{0},FlowReferenceKind::kRead,ExprId{0}},{{5},{0},FlowReferenceKind::kWrite,ExprId{1}}};
  const std::array queries{FlowUnitQuery{{0},{0},FlowPoint{2}}};
  const auto relation=[&] {return std::get<FlowUnitRelations>(AnalyzeFlowUnits(flow,queries)).queries[0];};
  Check(relation()==FlowUnitRelation::kEquivalent);
  Link(flow,5,2);Check(relation()==FlowUnitRelation::kDifferent); // raw repeats, source does not
  flow.bindings[0].allocation=FlowPoint{2};Check(relation()==FlowUnitRelation::kEquivalent);
  flow.bindings[0].cell_access=CellVisibility::kExternal;Check(relation()==FlowUnitRelation::kUnmodeled);
  flow.unit_captures_complete=true;flow.unit_captures.push_back({{4},{0}});Check(relation()==FlowUnitRelation::kEquivalent);
  flow.bindings[0].allocation=FlowPoint{4};Check(relation()==FlowUnitRelation::kDifferent);
  flow.bindings[0].allocation=FlowPoint{2};Link(flow,0,3); // allocation failure cannot lend a unit
  Check(relation()==FlowUnitRelation::kUnmodeled);
  // Unknown cleanup cannot certify a current unit even with a complete census.
  auto scopes=std::make_shared<FlowScopes>(*flow.scopes);flow.scopes=scopes;
  scopes->events[3].before=UnmodeledScope{};
  Check(relation()==FlowUnitRelation::kUnmodeled);
  // A capture is an observation even in the absence of scalar reads/writes.
  auto captures=Graph(4);for(unsigned i=0;i<3;++i)Link(captures,i,i+1);Link(captures,3,0);
  captures.unit_captures_complete=true;captures.bindings[0].cell_access=CellVisibility::kExternal;
  captures.events[1].kind=FlowEventKind::kEnterScope;captures.events[1].scope=ScopeId{0};
  captures.unit_captures={{{2},{0}}};const std::array source{FlowUnitQuery{{0},{0}}};
  Check(std::get<FlowUnitRelations>(AnalyzeFlowUnits(captures,source)).queries[0]==FlowUnitRelation::kDifferent);
}
void Scale() {
  std::uint64_t previous=0;
  for(unsigned count:{1U,200U,201U,400U,800U}) {
    auto flow=Graph(count*2+1,count);std::vector<FlowUnitQuery> queries;
    for(unsigned i=0;i<count*2;++i)Link(flow,i,i+1);
    for(unsigned i=0;i<count;++i) {
      flow.bindings[i].allocation=FlowPoint{i*2};
      flow.references.push_back({{i*2+1},{i},FlowReferenceKind::kRead,ExprId{i}});
      queries.push_back({{0},{i}});queries.push_back({{0},{i}});
    }
    const auto result=std::get<FlowUnitRelations>(AnalyzeFlowUnits(flow,queries));
    for(auto relation:result.queries)Check(relation==FlowUnitRelation::kEquivalent);
    if(count>=400)Check(result.work<previous*3);previous=result.work;
    std::cout<<"units "<<count<<" work="<<result.work<<" bytes="<<result.allocated_bytes<<'\n';
    if(count==200) {
      Check(std::holds_alternative<FlowUnitRelations>(AnalyzeFlowUnits(flow,queries,{result.allocated_bytes,result.work})));
      Check(std::get<TreeStructureError>(AnalyzeFlowUnits(flow,queries,{result.allocated_bytes-1,result.work}))==TreeStructureError::kResourceLimit);
      Check(std::get<TreeStructureError>(AnalyzeFlowUnits(flow,queries,{result.allocated_bytes,result.work-1}))==TreeStructureError::kWorkLimit);
    }
  }
}
}
int main(){Exhaustive();SuccessfulAllocation();Scale();}
