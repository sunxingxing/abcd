#include "analysis/guarded_fixture.h"
#include "../../../src/source/guarded_stores.h"
#include <iostream>
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
namespace {
auto Run(std::size_t count, bool copies = false, bool split = false) {
  return test::WithGuardedGraph(count,copies,split,false,[&](const auto& graph,const auto& body,const auto&){
    const auto reads=std::get<GuardedReads>(ProveGuardedReads(graph));
    const auto env=std::get<ValueEnvironments>(BuildPhasedValueEnvironments(graph));
    const auto dispatch=std::get<ExceptionDispatch>(BuildExceptionDispatch(body,abc::ProfileId::kDynamic1301));
    const auto build=[&](SemanticBudget limit=SemanticBudget{}){
      TreeCheckBudget budget(limit);
      const auto proof=BuildGuardedStores(graph,env,reads,dispatch,budget);
      VerifyGuardedStores(graph,env,reads,dispatch,proof,budget);
      return std::tuple{proof,budget.Bytes(),budget.Work()};
    };
    const auto [proof,bytes,work]=build();test::Check(proof.stores.size()==count);
    const auto rejected=[&](const auto& changed){try{TreeCheckBudget b({});VerifyGuardedStores(graph,env,reads,dispatch,changed,b);return false;}catch(GuardedStoreError){return true;}};
    auto wrong=proof;wrong.stores.front().slot.value++;test::Check(rejected(wrong));
    wrong=proof;wrong.stores.front().depth.value++;test::Check(rejected(wrong));
    wrong=proof;wrong.stores.front().input.value++;test::Check(rejected(wrong));
    wrong=proof;wrong.stores.front().value.value++;test::Check(rejected(wrong));
    wrong=proof;wrong.stores.front().handler.reset();test::Check(rejected(wrong));
    wrong=proof;wrong.stores.front().success.value++;test::Check(rejected(wrong));
    wrong=proof;wrong.stores.erase(wrong.stores.begin());test::Check(rejected(wrong));
    wrong=proof;wrong.stores.push_back(proof.stores.front());test::Check(rejected(wrong));
    // A wrong dispatch scope must keep Language.
    auto outside=dispatch;outside.spans.clear();TreeCheckBudget b({});
    test::Check(BuildGuardedStores(graph,env,reads,outside,b).stores.empty());
    auto missing_guard=reads;missing_guard.reads.clear();
    try{TreeCheckBudget check({});VerifyGuardedStores(graph,env,missing_guard,dispatch,proof,check);test::Check(false);}catch(GuardedStoreError){}
    auto changed=graph;const auto site=proof.stores.front().store;
    std::get<ValueLexicalStore>(changed.ops[site.value].operation).address.slot.value++;
    try{TreeCheckBudget check({});VerifyGuardedStores(changed,env,reads,dispatch,proof,check);test::Check(false);}catch(GuardedStoreError){}
    // Keeping the same UseId cannot hide a changed stored SSA operand.
    changed=graph;
    const auto different=std::find_if(graph.values.begin(),graph.values.end(),[&](const auto& value){
      return value.domain.IsLanguage() && &value != &graph.values[proof.stores.front().value.value];
    });
    test::Check(different!=graph.values.end());
    changed.uses[proof.stores.front().input.value].operand=ValueId{static_cast<std::uint32_t>(different-graph.values.begin())};
    try{TreeCheckBudget check({});VerifyGuardedStores(changed,env,reads,dispatch,proof,check);test::Check(false);}catch(GuardedStoreError){}
    test::Check(std::get<0>(build({bytes,work})).stores==proof.stores);
    for(const auto limit:{SemanticBudget{bytes-1,work},SemanticBudget{bytes,work-1}}){
      try{build(limit);test::Check(false);}catch(TreeStructureError e){test::Check(e==(limit.bytes<bytes?TreeStructureError::kResourceLimit:TreeStructureError::kWorkLimit));}
    }
    std::cout<<count<<" bytes="<<bytes<<" work="<<work<<'\n';return std::pair{bytes,work};
  },false,false,test::GuardedCallCase::kPlain,false,true);
}
}
int main(){
  for (const auto variant : {test::GuardedStoreCase::kEffect, test::GuardedStoreCase::kReset, test::GuardedStoreCase::kOutsideCatch}) {
    test::WithGuardedGraph(1, false, false, false, [&](const auto& graph, const auto& body, const auto&) {
      const auto reads = std::get<GuardedReads>(ProveGuardedReads(graph));
      const auto env = std::get<ValueEnvironments>(BuildPhasedValueEnvironments(graph));
      const auto dispatch = std::get<ExceptionDispatch>(BuildExceptionDispatch(body, abc::ProfileId::kDynamic1301));
      TreeCheckBudget budget({});
      const auto proof = BuildGuardedStores(graph, env, reads, dispatch, budget);
      if (variant == test::GuardedStoreCase::kReset) {
        // The intervening store is itself guarded, but the later store cannot
        // borrow its earlier guard across that write. The uncovered access
        // keeps the family requirement at Language.
        test::Check(proof.stores.size() == 1);
        OpId last{};
        for (std::size_t i = 0; i < graph.ops.size(); ++i)
          if (std::holds_alternative<ValueLexicalStore>(graph.ops[i].operation)) last = {static_cast<std::uint32_t>(i)};
        test::Check(proof.stores.front().store != last);
        auto forged = proof; forged.stores.front().store = last;
        try { TreeCheckBudget checked({}); VerifyGuardedStores(graph, env, reads, dispatch, forged, checked); test::Check(false); }
        catch (GuardedStoreError) {}
      } else test::Check(proof.stores.empty());
    }, false, false, test::GuardedCallCase::kPlain, false, true, variant);
  }
  Run(1);Run(1,true);Run(1,true,true);std::pair<std::size_t,std::uint64_t> previous{};
  for(const auto n:{200U,201U,400U,800U}){const auto cost=Run(n);if(n>200)test::Check(cost.first<3*previous.first&&cost.second<3*previous.second);previous=cost;}
}
