#include "../../../src/source/logical_evaluation.h"
#include "../../../src/source/evaluation_failure_json.h"
#include "../../support/analysis/ssa_fixture.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include <iostream>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
using test::Check;
namespace {
ValueProgram Graph(unsigned count, bool copies=false, bool split=false, bool swapped=false) {
  std::vector<std::uint8_t> bytes;
  const auto saved=static_cast<std::uint8_t>(swapped?0:1), array=static_cast<std::uint8_t>(swapped?1:0);
  for(unsigned i=0;i<count;++i) {
    bytes.insert(bytes.end(),{0x60,3,0x42,0,0,0,0x61,array,0x62,0,0,0,0,0x37,0,array,0x61,saved});
    if(copies) bytes.insert(bytes.end(),{0x45,2,saved,0x45,saved,2});
    if(split) bytes.insert(bytes.end(),{0x4d,2});
    bytes.insert(bytes.end(),{0x60,4,0x42,0,1,0,0x28,0,saved,0x61,2,0x23});
    const auto branch=bytes.size();bytes.insert(bytes.end(),{0x51,0});
    bytes.insert(bytes.end(),{0x60,4,0x42,0,2,0,0x28,0,saved,0x61,2});
    bytes[branch+1]=static_cast<std::uint8_t>(bytes.size()-branch);
    bytes.insert(bytes.end(),{0x60,2});
  }
  bytes.push_back(0x64);
  auto body=test::Body(bytes);
  auto semantic=BuildSemanticMethod(body,abc::ProfileId::kDynamic1206,5);
  Check(std::holds_alternative<SemanticMethod>(semantic));
  auto proof=VerifySemanticSource(body,std::get<SemanticMethod>(semantic),3,2);
  Check(std::holds_alternative<SemanticProof>(proof));
  auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(3,2,{}));
  auto graph=std::get<ValueProgram>(ImportNormalValues(body,std::get<SemanticMethod>(semantic),std::get<SemanticProof>(proof),frame));
  Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph,graph.revision)));
  return graph;
}
std::pair<OpId,UseId> Candidate(const ValueProgram& graph,const EvaluationPlan& plan) {
  for(std::size_t i=0;i<graph.ops.size();++i) {
    if(!std::holds_alternative<ValuePropertyRead>(graph.ops[i].operation)) continue;
    for(auto use=graph.values[graph.ops[i].result->value].first_use;use;use=graph.uses[use->value].next) {
      const auto* owner=std::get_if<OpId>(&graph.uses[use->value].owner);
      if(owner && plan.operations[owner->value].kind==EvaluationKind::kLogicalLeft)
        return {OpId{static_cast<std::uint32_t>(i)},*use};
    }
  }
  Check(false);std::abort();
}
bool SamePlacements(const EvaluationPlan& a,const EvaluationPlan& b) {
  if(a.operations.size()!=b.operations.size())return false;
  for(std::size_t i=0;i<a.operations.size();++i) {
    const auto& x=a.operations[i];const auto& y=b.operations[i];
    if(x.kind!=y.kind||x.consumer!=y.consumer||x.temporary!=y.temporary)return false;
  }
  return true;
}
void Json() {
  for(unsigned i=0;i<=static_cast<unsigned>(SnapshotCandidateReason::kConsumerPlacement);++i) {
    EvaluationEvidence e;e.obligation=EvaluationObligation::kSnapshotCandidate;
    e.location.operation=OpId{0};e.location.pc=abc::CodeOffset{0};
    e.snapshot_candidate=SnapshotCandidateEvidence{static_cast<SnapshotCandidateReason>(i),UseId{0},EvaluationKind::kRoot,EvaluationKind::kLogicalLeft};
    Check(ParseEvaluationFailure(EvaluationFailureJson(e))==e);
    e.location.pc.reset();e.snapshot_candidate->occurrence.reset();
    Check(ParseEvaluationFailure(EvaluationFailureJson(e))==e);
  }
  auto invalid=EvaluationFailureJson(EvaluationEvidence{});
  invalid["snapshot_candidate"]={{"reason",255}};Check(!ParseEvaluationFailure(invalid));
}
void Case(unsigned count,bool copies=false,bool split=false,bool swapped=false) {
  auto graph=Graph(count,copies,split,swapped);
  auto built=BuildEvaluationPlan(graph);
  if(const auto* f=std::get_if<EvaluationFailure>(&built)) std::cerr<<EvaluationFailureJson(f->evaluation)<<'\n';
  Check(std::holds_alternative<EvaluationPlan>(built));
  auto plan=std::get<EvaluationPlan>(built);
  Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(graph,plan)));
  const auto [op,use]=Candidate(graph,plan);
  Check(plan.operations[op.value].kind==EvaluationKind::kRoot);
  auto forged=plan;forged.operations[op.value]={EvaluationKind::kPredicateSnapshot,use,TemporaryReason::kMultipleUses};
  auto bad=VerifyEvaluationPlan(graph,forged);Check(std::holds_alternative<EvaluationFailure>(bad));
  if(!split) {
    const auto& f=std::get<EvaluationFailure>(bad);
    Check(f.evaluation.obligation==EvaluationObligation::kSnapshotCandidate);
    Check(f.evaluation.snapshot_candidate->reason==SnapshotCandidateReason::kConsumerPlacement);
    Check(f.evaluation.snapshot_candidate->consumer==EvaluationKind::kLogicalLeft);
    TreeCheckBudget budget({128*1024*1024,100000000});
    CommitPredicateSnapshots(graph,forged,budget);Check(SamePlacements(forged,plan));
    auto exact=plan;exact.operations[op.value]={EvaluationKind::kPredicateSnapshot,use,TemporaryReason::kMultipleUses};
    TreeCheckBudget precise({budget.Bytes(),budget.Work()});CommitPredicateSnapshots(graph,exact,precise);
    Check(SamePlacements(exact,plan));
    for(bool bytes:{false,true}) {
      auto short_plan=plan;short_plan.operations[op.value]={EvaluationKind::kPredicateSnapshot,use,TemporaryReason::kMultipleUses};
      TreeCheckBudget short_budget({budget.Bytes()-(bytes?1:0),budget.Work()-(bytes?0:1)});
      bool refused=false;try{CommitPredicateSnapshots(graph,short_plan,short_budget);}catch(TreeStructureError error){refused=error==(bytes?TreeStructureError::kResourceLimit:TreeStructureError::kWorkLimit);}
      Check(refused);
    }
    Check(budget.Work()<100*(graph.ops.size()+graph.uses.size()));
  }
  auto wrong=plan;wrong.operations[op.value]={EvaluationKind::kPredicateSnapshot,UseId{0},TemporaryReason::kMultipleUses};
  Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph,wrong)));
  wrong=plan;wrong.operations[op.value]={EvaluationKind::kPredicateSnapshot,{},TemporaryReason::kMultipleUses};
  Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph,wrong)));
  // Two distinct producers cannot claim the same saving occurrence.
  for(std::size_t i=0;i<graph.ops.size();++i) {
    if(!std::holds_alternative<ValueNamedRead>(graph.ops[i].operation))continue;
    wrong=plan;wrong.operations[op.value]={EvaluationKind::kPredicateSnapshot,use,TemporaryReason::kMultipleUses};
    wrong.operations[i]={EvaluationKind::kPredicateSnapshot,use,TemporaryReason::kMultipleUses};
    Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph,wrong)));break;
  }
  wrong=plan;wrong.operations[op.value]={EvaluationKind::kElidedPure,{},{}};
  Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph,wrong)));
}
}
int main(){Json();for(bool c:{false,true})for(bool s:{false,true})for(bool r:{false,true})Case(1,c,s,r);for(unsigned n:{200,201,400,800})Case(n);}
