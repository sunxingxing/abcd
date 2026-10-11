#include "../../../src/source/recovery_flow.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <source_location>
using namespace abcd;using namespace abcd::source;
namespace {
void Check(bool good,std::source_location at=std::source_location::current()) {
  if(!good){std::cerr<<"snapshot flow check "<<at.line()<<'\n';std::abort();}
}
struct Cost {std::uint64_t work;std::size_t bytes;};
Cost Run(unsigned count,bool reverse) {
  FunctionTree tree{};tree.callable=CallableKind::kFunction;tree.strictness=Strictness::kStrict;
  tree.parameter_scope={0};tree.body_scope={1};tree.body={0};
  tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
  tree.origins={OriginalSite{{0},{0}}};
  for(unsigned i=0;i<count;++i) {
    tree.symbols.push_back({{0},{},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kSynthesized,{}});
    tree.parameters.push_back({{i},{},false});
    tree.arguments.push_back(ExprId{i});tree.expressions.push_back({SymbolExpr{{i}},{0}});
  }
  ExprId snapshot{count};tree.expressions.push_back({ArgumentSnapshotExpr{tree.owner,{0,count}},{0}});
  if(reverse) {
    for(auto& argument:tree.arguments)argument.value=count-argument.value;
    snapshot={0};std::reverse(tree.expressions.begin(),tree.expressions.end());
  }
  tree.statements={{{BlockStmt{{1},0,1}},{0}},{{ReturnStmt{snapshot}},{0}}};tree.children={{1}};
  std::vector<std::optional<SymbolId>> aliases(count);std::vector<std::optional<PrimitiveId>> literals(count);
  std::vector<std::optional<ExprId>> replacements(tree.expressions.size());
  std::vector<std::optional<ExpressionTransport>> transports(tree.expressions.size());
  std::vector<std::uint8_t> omitted(tree.statements.size()),normal(tree.expressions.size());
  std::vector<ExpressionEvaluation> effects(tree.expressions.size(),ExpressionEvaluation::kRequired);
  LegacyFlowProjection projection{aliases,literals,replacements,omitted,effects,normal};projection.transports=transports;
  auto flow=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  const Cost cost{flow.work,flow.allocated_bytes};
  Check(flow.snapshot_slots.size()==count);
  // Independent concrete invocation oracle: only the present argument prefix
  // may be read, in order. Choose semantic edges, never physical edge order.
  std::vector<std::optional<unsigned>> branches(flow.events.size());
  for(const auto& slot:flow.snapshot_slots){Check(slot.snapshot==snapshot);branches[slot.branch.value]=slot.ordinal;}
  for(unsigned arity:{0U,1U,count/2,count,count+1}) {
    std::vector<unsigned> reads;auto point=flow.entry;unsigned steps=0,snapshots=0;
    for(;;) {
      Check(++steps<=flow.events.size());const auto& event=flow.events[point.value];
      if(event.kind==FlowEventKind::kRead){Check(event.read.has_value());reads.push_back(event.read->value);}
      if(event.kind==FlowEventKind::kEvaluation&&event.expression==snapshot)++snapshots;
      std::optional<FlowPoint> next;
      for(auto edge=event.outgoing;edge;edge=flow.edges[*edge].outgoing) {
        const auto& route=flow.edges[*edge];
        const bool selected=branches[point.value]?
            route.completion==(arity>*branches[point.value]?FlowCompletion::kTaken:FlowCompletion::kNotTaken):
            route.completion==FlowCompletion::kNormal||route.completion==FlowCompletion::kReturn;
        if(selected){Check(!next);next=route.to;}
      }
      if(!next)break;point=*next;
    }
    Check(snapshots==1&&reads.size()==std::min(arity,count));
    for(unsigned i=0;i<reads.size();++i)Check(reads[i]==i);
  }
  Check(std::holds_alternative<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{cost.bytes,cost.work})));
  Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{cost.bytes-1,cost.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{cost.bytes,cost.work-1}))==TreeStructureError::kWorkLimit);
  // Recycled storage must not retain branch facts from an earlier proposal.
  // This deliberately inequivalent omission tests withdrawal, not admission.
  transports[snapshot.value]=ExpressionTransport{snapshot,PrimitiveId{0}};
  flow=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{},std::move(flow)));
  Check(flow.snapshot_slots.empty());
  transports[snapshot.value].reset();
  flow=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{},std::move(flow)));
  Check(flow.snapshot_slots.size()==count);
  return cost;
}
}
int main() {
  for(bool reverse:{false,true}) {
    Cost previous{};
    for(unsigned count:{1U,2U,4U,200U,201U,400U,800U}) {
      const auto cost=Run(count,reverse);
      if(count>=201){const auto growth=count==201?1.1:3.0;Check(cost.work<=growth*previous.work&&cost.bytes<=growth*previous.bytes);}
      std::cout<<count<<' '<<reverse<<' '<<cost.work<<' '<<cost.bytes<<'\n';previous=cost;
    }
  }
}
