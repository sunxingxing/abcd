#include "abcd/source/tree_printer.h"
#include "abcd/source/counting_sink.h"
#include "../../../src/source/tree_print_trace.h"
#include "../../../src/source/tree_checks.h"
#include "../../../src/source/tree_print_failure_json.h"
#include "../../../src/source/family_route_failure.h"
#include "../../../tools/record_failure_json.h"
#include <sstream>
#include <source_location>
#include <iostream>
#include <cstdlib>
using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool ok,std::source_location where=std::source_location::current()) {
  if(!ok){std::cerr<<where.line()<<'\n';std::abort();}
}
FunctionTree Tree(std::size_t count) {
  FunctionTree t{};t.method={123};t.owner={7};t.callable=CallableKind::kFunction;t.strictness=Strictness::kStrict;
  t.parameter_scope={0};t.body_scope={1};t.body={0};
  t.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
  t.origins={EntryOrigin{},OriginalSite{{0},{17},abc::CodeOffset{0}}};
  t.statements.push_back({BlockStmt{{1},0,static_cast<std::uint32_t>(count)},{0}});
  for(std::size_t i=0;i<count;++i){t.children.push_back({static_cast<std::uint32_t>(i+1)});t.statements.push_back({DebuggerStmt{},{1}});}
  return t;
}
void RoundTrip(const TreePrintFailure& f) {
  const auto json=TreePrintFailureJson(f);
  const auto parsed=ParseTreePrintFailure(nlohmann::json::parse(json.dump()));
  Check(parsed.has_value());Check(TreePrintFailureJson(*parsed)==json);
  auto route=FamilyRouteFailure(FamilyEmitFailure{f});
  MethodRouteReceipt receipt{};receipt.identity.file_size=4096;receipt.identity.file_hash.bytes[0]=1;
  receipt.identity.abc_version={{12,0,6,0}};receipt.identity.profile=abc::ProfileId::kDynamic1206;
  receipt.method={123};receipt.record=abc::EntityOffset{77};receipt.route=SourceRoute::kValueTree;
  receipt.work_revision=0;receipt.stage=SourceStage::kTreeVerified;receipt.coverage=Coverage::kUnsupported;receipt.failure=route;
  auto encoded=SerializeRouteReceipt(receipt);Check(std::holds_alternative<std::string>(encoded));
  auto decoded=ParseRouteReceipt(std::get<std::string>(encoded));Check(std::holds_alternative<MethodRouteReceipt>(decoded));
  Check(TreePrintFailureJson(*std::get<MethodRouteReceipt>(decoded).failure->printing)==json);
  RecordProgramFailure entry{RecordProgramError::kResourceLimit,{}};entry.printing=f;entry.causes=route.causes;
  nlohmann::json a,b;tools::DescribeRecordFailure(entry,a);tools::DescribeRecordFailure(RouteRefusal{route},b);
  Check(a["source_printing"]==b["source_failure"]["printing"]);
  Check(a["source_causes"]==b["source_failure"]["causes"]);
}
}
class AllocationSink final:public std::streambuf {
  std::streamsize xsputn(const char*,std::streamsize) override {throw std::bad_alloc();}
};
int main() {
  static_assert(std::is_trivially_copyable_v<TreePrintFailure>);
  static_assert(static_cast<unsigned>(TreePrintError::kResourceLimit)==3);
  for(unsigned k=0;k<=static_cast<unsigned>(TreePrintBudgetKind::kHostAllocation);++k)
    for(unsigned site=0;site<=static_cast<unsigned>(TreePrintSite::kPublication);++site) {
      TreePrintFailure f{TreePrintError::kResourceLimit};f.site=static_cast<TreePrintSite>(site);
      f.budget=TreeBudgetEvidence{static_cast<TreePrintBudgetKind>(k),17,16,32,16,24,16};
      f.location.method=abc::EntityOffset{123};f.owner=FunctionOwnerId{7};f.anchor=AnchorId{0};
      f.location.pc=abc::CodeOffset{0};f.location.working_pc=abc::CodeOffset{17};f.location.expression=ExprId{0};
      f.cause=TreeStructureError::kResourceLimit;RoundTrip(f);
      f.location.pc.reset();f.location.working_pc.reset();f.budget->requested.reset();RoundTrip(f);
    }
  for(auto kind:{TreePrintError::kResourceLimit,TreePrintError::kWorkLimit,TreePrintError::kOutputLimit,TreePrintError::kWrite,TreePrintError::kUnsupportedContext}) {
    TreePrintFailure failure{kind};failure.cause=TreeStructureError::kResourceLimit;
    const auto expected=kind==TreePrintError::kResourceLimit?RouteFailureClass::kMemoryLimit:
      kind==TreePrintError::kWorkLimit?RouteFailureClass::kWorkLimit:
      (kind==TreePrintError::kOutputLimit||kind==TreePrintError::kWrite)?RouteFailureClass::kOutputFailure:RouteFailureClass::kUnproved;
    Check(FamilyRouteFailure(FamilyEmitFailure{failure}).failure_class==expected);
  }
  // Real lower ledger, including release/peak distinction and overflow.
  {TreePrintTrace trace;TreeCheckBudget b({32,4});b.Admit(24,1);b.Release(8);
    try{b.Admit(17,1);Check(false);}catch(TreeStructureError e){Check(e==TreeStructureError::kResourceLimit);}
    const auto& e=*trace.failure.budget;Check(e.requested==17&&e.remaining==16&&e.limit==32&&e.used==16&&e.peak==24&&e.retained==16);
    try{b.Admit(SIZE_MAX,2);Check(false);}catch(TreeStructureError){}
    Check(trace.failure.budget->budget_kind==TreePrintBudgetKind::kCapacityOverflow&&!trace.failure.budget->requested);
    b.Step(2);
    try{b.Step(3);Check(false);}catch(TreeStructureError e){Check(e==TreeStructureError::kWorkLimit);}
    Check(trace.failure.budget->requested==3&&trace.failure.budget->remaining==2&&trace.failure.budget->peak==2);
  }
  {
    auto tree=Tree(1);AllocationSink sink;std::ostream output(&sink);output.exceptions(std::ios::badbit);
    auto result=PrintFunctionTree(tree,output);const auto& f=std::get<TreePrintFailure>(result);
    Check(f.kind==TreePrintError::kResourceLimit&&f.budget&&f.budget->budget_kind==TreePrintBudgetKind::kHostAllocation);
    Check(!f.budget->requested&&f.site==TreePrintSite::kTokenOutput);RoundTrip(f);
    bool pc_zero=false;
    for(std::size_t limit=0;limit<80;++limit){
      CountingSink counter;std::ostream short_output(&counter);auto denied=PrintFunctionTree(tree,short_output,{limit});
      if(const auto* failure=std::get_if<TreePrintFailure>(&denied))if(failure->location.pc==abc::CodeOffset{0}) {
        Check(failure->location.working_pc==abc::CodeOffset{17});pc_zero=true;
      }
    }
    Check(pc_zero);
  }
  for(const auto count:{1U,200U,201U,400U,800U}) {
    auto tree=Tree(count);std::ostringstream real;
    auto printed=PrintFunctionTree(tree,real);Check(std::holds_alternative<PrintedFunction>(printed));
    CountingSink sink;std::ostream counted(&sink);auto measured=PrintFunctionTree(tree,counted);
    Check(std::holds_alternative<PrintedFunction>(measured));Check(sink.Bytes()==real.str().size());
    Check(std::get<PrintedFunction>(printed).allocated_bytes==std::get<PrintedFunction>(measured).allocated_bytes);
    std::cout<<"scale "<<count<<" bytes "<<sink.Bytes()<<" charged_peak "<<std::get<PrintedFunction>(printed).allocated_bytes<<" work "<<std::get<PrintedFunction>(printed).work<<'\n';
    CountingSink exact_sink;std::ostream exact(&exact_sink);
    const auto peak=std::get<PrintedFunction>(printed).allocated_bytes;
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,exact,{real.str().size()},{peak})));
    CountingSink denied_sink;std::ostream short_scratch(&denied_sink);
    auto boundary=PrintFunctionTree(tree,short_scratch,{}, {peak-1});
    Check(std::holds_alternative<TreePrintFailure>(boundary));
    Check(std::get<TreePrintFailure>(boundary).kind==TreePrintError::kResourceLimit);
    std::ostringstream limited;auto denied=PrintFunctionTree(tree,limited,{real.str().size()-1});
    Check(std::holds_alternative<TreePrintFailure>(denied));const auto& f=std::get<TreePrintFailure>(denied);
    Check(f.kind==TreePrintError::kOutputLimit&&f.budget&&f.budget->budget_kind==TreePrintBudgetKind::kOutputBytes);
    Check(f.site==TreePrintSite::kTokenOutput);RoundTrip(f);
    auto scratch=PrintFunctionTree(tree,limited,{}, {0});const auto& g=std::get<TreePrintFailure>(scratch);
    Check(g.kind==TreePrintError::kResourceLimit&&g.budget&&g.budget->requested&&g.budget->remaining==0);
    Check(g.location.method==tree.method&&g.owner==tree.owner);Check(std::get<TreeStructureError>(g.cause)==TreeStructureError::kResourceLimit);RoundTrip(g);
  }
}
