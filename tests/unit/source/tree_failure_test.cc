#include "../../../src/source/family_route_failure.h"
#include "../../../src/source/tree_failure_json.h"
#include "../../../tools/record_failure_json.h"
#include <cstdlib>
#include <iostream>
#include <source_location>
using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool v, std::source_location at = std::source_location::current()) {
  if (!v) { std::cerr << "tree_failure_test:" << at.line() << '\n'; std::abort(); }
}
MethodRouteReceipt Receipt(RouteFailure failure) {
  MethodRouteReceipt r;
  r.identity.file_size=4096; r.identity.file_hash.bytes[0]=1;
  r.identity.abc_version={{12,0,6,0}}; r.identity.profile=abc::ProfileId::kDynamic1206;
  r.method=abc::EntityOffset{777}; r.record=abc::EntityOffset{111};
  r.route=SourceRoute::kValueTree; r.stage=SourceStage::kValuesVerified; r.work_revision=0; r.coverage=Coverage::kUnsupported;
  r.failure=failure; return r;
}
void RoundTrip(const RouteFailure& failure) {
  auto encoded=SerializeRouteReceipt(Receipt(failure));
  Check(std::holds_alternative<std::string>(encoded));
  auto decoded=ParseRouteReceipt(std::get<std::string>(encoded));
  Check(std::holds_alternative<MethodRouteReceipt>(decoded));
  const auto& actual=*std::get<MethodRouteReceipt>(decoded).failure;
  Check(actual.tree==failure.tree);
  Check(actual.evaluation==failure.evaluation);
  Check(actual.pc==failure.pc && actual.working_pc==failure.working_pc && actual.method==failure.method);
  if(failure.deferred) Check(DeferredCellJson(*actual.deferred)==DeferredCellJson(*failure.deferred));
  Check(actual.causes.count==failure.causes.count);
  nlohmann::json routed; tools::DescribeRecordFailure(RouteRefusal{failure},routed);
  RecordProgramFailure entry{RecordProgramError::kFunction, failure.pc};
  entry.evaluation=failure.evaluation;entry.tree=failure.tree;entry.deferred=failure.deferred;entry.causes=failure.causes;
  entry.method=failure.method;entry.working_pc=failure.working_pc;
  nlohmann::json recorded; tools::DescribeRecordFailure(entry,recorded);
  if(failure.evaluation) Check(recorded["source_evaluation"]==routed["source_failure"]["evaluation"]);
  if(failure.tree) {
    Check(recorded["source_tree"]==routed["source_failure"]["tree"]);
    const auto parsed=ParseTreeFailure(nlohmann::json::parse(recorded["source_tree"].dump()));
    Check(parsed==failure.tree);
  }
  if(failure.deferred) Check(recorded["source_deferred"]==routed["source_failure"]["deferred"]);
  Check(recorded.contains("source_pc")==failure.pc.has_value());
  Check(recorded["source_causes"]==routed["source_failure"]["causes"]);
}
}
int main() {
  static_assert(std::is_trivially_copyable_v<EvaluationFailure>);
  static_assert(static_cast<unsigned>(EvaluationError::kOwnership)==5);
  static_assert(static_cast<unsigned>(EvaluationError::kWorkLimit)==8);
  static_assert(static_cast<unsigned>(EvaluationError::kResourceLimit)==9);
  // Every obligation travels through both publication paths. Zero is a real
  // PC/ID; absence and synthetic origins must survive independently.
  for(unsigned tag=0;tag<=static_cast<unsigned>(TreeObligation::kPlacement);++tag) {
    FunctionTreeFailure f{FunctionTreeError::kControl};
    f.tree=TreeFailureEvidence{static_cast<TreeObligation>(tag),{}};
    auto& loc=f.tree->location;loc.method=abc::EntityOffset{77};loc.operation=analysis::OpId{0};
    loc.block=analysis::WorkBlockId{0};loc.edge=analysis::EdgeId{0};loc.expression=ExprId{0};loc.statement=StmtId{0};
    loc.control=ControlId{0};loc.pc=abc::CodeOffset{0};loc.working_pc=abc::CodeOffset{17};
    f.tree->ownership=ExpressionOwnershipError::kMissingExpression;
    f.tree->placement=EvaluationKind::kCellGuard;f.tree->decision_entry=TreeLocation{};
    f.tree->expected_owner=loc;f.tree->actual_owner=TreeLocation{};
    f.cause=ControlError::kUncoveredBlock;
    auto projected=FamilyRouteFailure(f);
    Check(projected.pc==abc::CodeOffset{0} && projected.working_pc==abc::CodeOffset{17});
    Check(projected.causes.count==4 && projected.failure_class==RouteFailureClass::kUnproved);
    RoundTrip(projected);
    f.tree->location={};f.tree->location.method=abc::EntityOffset{77};
    f.tree->location.synthetic=TreeSyntheticLocation{ControlOriginKind::kTryExit,0};
    f.tree->location.control=ControlId{0};
    projected=FamilyRouteFailure(f);Check(!projected.pc && !projected.working_pc);RoundTrip(projected);
    auto forged=TreeFailureJson(*f.tree);forged["location"]["pc"]=0U;
    Check(!ParseTreeFailure(forged));
  }
  for(auto reason:{DeferredCellError::kExternalRoot,DeferredCellError::kPhasedParent,DeferredCellError::kPlacement,DeferredCellError::kBinding}) {
    RouteFailure f{RouteFailureClass::kUnproved,ValueRouteError::kTreeStage,{}};
    f.deferred=DeferredCellEvidence{reason};auto& d=*f.deferred;
    d.creator=abc::EntityOffset{91};d.child=abc::EntityOffset{77};d.cell=CellId{0};d.requested=analysis::CaptureDomain::kGuarded;
    d.occurrence=CaptureOccurrence{0,{{0},{0}},analysis::CaptureDomain::kGuarded,CaptureOrigin{{77},abc::CodeOffset{0},abc::CodeOffset{5}}};
    d.allocation.operation=analysis::OpId{0};d.store.operation=analysis::OpId{3};d.creation.operation=analysis::OpId{1};
    RoundTrip(f);
  }
  for(unsigned tag=0;tag<=static_cast<unsigned>(EvaluationObligation::kUse);++tag) {
    EvaluationFailure f{EvaluationError::kOwnership}; auto& e=f.evaluation;
    e.obligation=static_cast<EvaluationObligation>(tag);e.location.method=abc::EntityOffset{77};
    e.location.operation=analysis::OpId{0};e.location.pc=abc::CodeOffset{0};e.location.working_pc=abc::CodeOffset{17};
    e.use=analysis::UseId{0};e.value=analysis::ValueId{0};e.expected_consumer=analysis::UseId{1};
    e.actual_consumer=analysis::UseId{2};e.placement=EvaluationKind::kInline;e.temporary=TemporaryReason::kCrossRegionValue;
    e.expected_owner=e.location;e.actual_owner=TreeLocation{};
    e.selection_exit=SelectionExitReason::kCrossBlock;e.selection_edge=analysis::EdgeId{0};
    RoundTrip(FamilyRouteFailure(f));
    e.location={};e.location.synthetic=TreeSyntheticLocation{ControlOriginKind::kTryExit,0};
    RoundTrip(FamilyRouteFailure(f));
    for(auto kind:{EvaluationError::kResourceLimit,EvaluationError::kWorkLimit}) {
      f.kind=kind;const auto route=FamilyRouteFailure(f);
      Check(route.failure_class==(kind==EvaluationError::kResourceLimit?RouteFailureClass::kMemoryLimit:RouteFailureClass::kWorkLimit));
      Check(route.causes.count==2);RoundTrip(route);
    }
  }
  // The original leaf is retained, including resource/work classification.
  for(auto cause:{ControlError::kUncoveredBlock,ControlError::kResourceLimit,ControlError::kWorkLimit}) {
    FunctionTreeFailure f{cause==ControlError::kResourceLimit?FunctionTreeError::kResourceLimit:
        cause==ControlError::kWorkLimit?FunctionTreeError::kWorkLimit:FunctionTreeError::kControl}; f.cause=cause;
    auto route=FamilyRouteFailure(f);
    Check(route.causes.count==2);
    Check(route.failure_class==(cause==ControlError::kResourceLimit?RouteFailureClass::kMemoryLimit:
        cause==ControlError::kWorkLimit?RouteFailureClass::kWorkLimit:RouteFailureClass::kUnproved));
  }
  for(auto cause:{ExceptionControlError::kResourceLimit,ExceptionControlError::kWorkLimit}) {
    FunctionTreeFailure f{cause==ExceptionControlError::kResourceLimit?FunctionTreeError::kResourceLimit:FunctionTreeError::kWorkLimit};f.cause=cause;
    Check(FamilyRouteFailure(f).failure_class==(cause==ExceptionControlError::kResourceLimit?RouteFailureClass::kMemoryLimit:RouteFailureClass::kWorkLimit));
  }
  FunctionTreeFailure completion{FunctionTreeError::kWorkLimit};completion.cause=CompletionError::kWorkLimit;
  Check(FamilyRouteFailure(completion).failure_class==RouteFailureClass::kWorkLimit);
  FunctionTreeFailure flow{FunctionTreeError::kSemantics};
  flow.cause=TreeSemanticFailure{TreeSemanticError::kFlow,TreeFlowError::kPlacement};
  Check(FamilyRouteFailure(flow).causes.count==3);
  flow.cause=TreeFlowError::kWorkLimit;
  Check(FamilyRouteFailure(flow).failure_class==RouteFailureClass::kUnproved); // historical completion catch
  for (const auto cause : std::vector<decltype(PhasedFlowFailure::cause)>{
      analysis::SemanticFailure{analysis::SemanticError::kUnsupportedInstruction,{0}},
      analysis::FlowFailure{analysis::FlowError::kInvalidTarget,{0}},
      analysis::DispatchFailure{analysis::DispatchError::kInvalidBoundary,{0}}}) {
    PhasedFlowFailure input{PhasedMethodError::kInput,cause,{91},abc::CodeOffset{0},abc::CodeOffset{17}};
    auto route=FamilyRouteFailure(FamilyPrepareFailure{FamilyFileFailure{PhasedFileFailure{input},
        abc::EntityOffset{99},abc::EntityOffset{111},abc::CodeOffset{9}}});
    Check(route.method==abc::EntityOffset{91} && route.pc==abc::CodeOffset{0} && route.working_pc==abc::CodeOffset{17});
    Check(route.creation_method==abc::EntityOffset{111} && route.creation_pc==abc::CodeOffset{9});
    Check(route.causes.count==2);
    input.kind=PhasedMethodError::kResourceLimit;
    input.cause=analysis::SemanticFailure{analysis::SemanticError::kResourceLimit,{0}};
    input.pc.reset();input.working_pc.reset();
    route=FamilyRouteFailure(FamilyPrepareFailure{FamilyFileFailure{PhasedFileFailure{input}}});
    Check(route.failure_class==RouteFailureClass::kMemoryLimit && !route.pc && !route.working_pc);
  }
  // The initializer's leaf owner must override the stale enclosing method.
  FunctionInputFailure input{analysis::SemanticProofFailure{analysis::SemanticProofError::kInternalValue,{12}},
      abc::EntityOffset{91},abc::CodeOffset{12}};
  auto actual=FamilyRouteFailure(FamilyPrepareFailure{FamilyFileFailure{FunctionFileFailure{input},abc::EntityOffset{99}}});
  Check(actual.method==abc::EntityOffset{91} && actual.pc==abc::CodeOffset{12});
  // Normalize PC once using immutable origin identity; no guessed byte offset.
  analysis::ValueProgram graph;graph.origins.push_back({{0},{5},abc::CodeOffset{0}});
  analysis::ValueOp op{};op.origin={0};op.block={3};graph.ops.push_back(op);
  auto loc=OperationLocation(graph,{0},abc::EntityOffset{77});
  Check(loc.pc==abc::CodeOffset{0} && loc.working_pc==abc::CodeOffset{5} && loc.block==analysis::WorkBlockId{3});
  graph.origins[0].original.reset();loc=OperationLocation(graph,{0});
  Check(loc.pc==abc::CodeOffset{5} && loc.working_pc==abc::CodeOffset{5});
  Check(!OperationLocation(graph,{9}).pc);
  std::cout<<"tree_failure_test: ok\n";
}
