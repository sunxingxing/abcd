#include "../../../src/source/concurrent_completion.h"
#include "../../../src/source/concurrent_boundary.h"
#include "abcd/analysis/value_import.h"
#include "../../support/analysis/ssa_fixture.h"
using namespace abcd; using namespace source; using namespace analysis; using test::Check;
ValueProgram Graph(std::span<const std::uint8_t> bytes) {
 const auto body=test::Body(bytes);
 const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(0,2,{}));
 const auto input=std::get<SemanticMethod>(BuildSemanticMethod(body,abc::ProfileId::kDynamic1206,frame.registers));
 const auto proof=std::get<SemanticProof>(VerifySemanticSource(body,input,frame.registers,frame.locals));
 return std::get<ValueProgram>(ImportNormalValues(body,input,proof,frame));
}
ValueProgram Graph(std::initializer_list<std::uint8_t> bytes) {return Graph(std::span(bytes.begin(),bytes.size()));}
int main(){
 auto graph=Graph({0x60,0,0xfb,0,0x64});
 auto proof=std::get<ConcurrentCompletion>(BuildConcurrentCompletion(graph));
 Check(proof.returns.size()==1 && proof.returns[0].notification==OpId{1} && proof.returns[0].returned==OpId{2});
 Check(std::holds_alternative<std::uint64_t>(VerifyConcurrentCompletion(graph,proof)));
 CellPlan cells;cells.cells.push_back({AllocatedCell{{0},{0}},0,0});
 Check(std::holds_alternative<std::uint64_t>(VerifyConcurrentBoundary(graph,&cells)));
 cells.cells.push_back({IncomingCell{{0},{1}},0,0}); // Pure forwarding has no accesses, but crosses the boundary.
 Check(std::get<ConcurrentFailure>(VerifyConcurrentBoundary(graph,&cells)).kind==ConcurrentError::kCaptures);
 auto bad=proof;bad.returns[0].returned={0};Check(std::holds_alternative<ConcurrentFailure>(VerifyConcurrentCompletion(graph,bad)));
 bad=proof;bad.members[1].reset();Check(std::holds_alternative<ConcurrentFailure>(VerifyConcurrentCompletion(graph,bad)));
 bad=proof;bad.uses[graph.ops[1].first_use.value]=0;Check(std::holds_alternative<ConcurrentFailure>(VerifyConcurrentCompletion(graph,bad)));
 bad=proof;bad.returns.clear();Check(std::holds_alternative<ConcurrentFailure>(VerifyConcurrentCompletion(graph,bad)));
 auto reject=[](ValueProgram g,ConcurrentError error){auto result=BuildConcurrentCompletion(g);Check(std::holds_alternative<ConcurrentFailure>(result));Check(std::get<ConcurrentFailure>(result).kind==error);};
 auto early=Graph({0x60,0,0xfb,0,0x61,1,0x64});early.ops[2].normal=LanguageEffect::kUserCode;
 reject(early,ConcurrentError::kEarlyNotification);
 reject(Graph({0x60,0,0x64}),ConcurrentError::kMissingNotification);
 reject(Graph({0x60,0,0xfb,0,0xfb,0,0x64}),ConcurrentError::kDuplicateNotification);
 reject(Graph({0x60,0,0xfb,0,0x60,1,0x64}),ConcurrentError::kNotificationValue);
 auto wrong=graph;std::get<ValueConcurrentNotification>(wrong.ops[1].operation).owner=static_cast<NotificationOwner>(1);
 reject(wrong,ConcurrentError::kNotificationOwner);
 for(auto g:{Graph({0x60,0,0xfb,0,0x61,1,0x60,1,0x64}),Graph({0x60,0,0xfb,0,0x4d,2,0x64}),Graph({0x00,0xfb,0,0x65})}) {
   auto p=BuildConcurrentCompletion(g);Check(std::holds_alternative<ConcurrentCompletion>(p));
 }
 for(unsigned n:{200u,201u,400u,800u}) {
   std::vector<std::uint8_t> bytes;
   for(unsigned i=0;i<n;++i) {bytes.push_back(0x60);bytes.push_back(0);bytes.push_back(0x61);bytes.push_back(1);}
   bytes.insert(bytes.end(),{0xfb,0,0x64});
   auto g=Graph(std::span(bytes));
   auto p=std::get<ConcurrentCompletion>(BuildConcurrentCompletion(g));
   Check(p.returns.size()==1&&g.ops.size()>=n);
   Check(p.work<200ull*(g.ops.size()+g.values.size()+g.uses.size()+g.blocks.size()));
   Check(std::holds_alternative<ConcurrentCompletion>(BuildConcurrentCompletion(g,{p.allocated_bytes,p.work})));
   Check(std::get<ConcurrentFailure>(BuildConcurrentCompletion(g,{p.allocated_bytes-1,p.work})).kind==ConcurrentError::kResourceLimit);
   Check(std::get<ConcurrentFailure>(BuildConcurrentCompletion(g,{p.allocated_bytes,p.work-1})).kind==ConcurrentError::kWorkLimit);
 }
}
