#include "../../../src/source/family_route_failure.h"
#include "../../../src/source/async_failure_json.h"
#include "../../../tools/record_failure_json.h"
#include "../../../src/source/async_handlers.h"
#include "../../support/analysis/ssa_fixture.h"
#include <iostream>
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
namespace {
abc::MethodBody Body(unsigned n=0) {
  const auto op=[](abc::Opcode v){return static_cast<std::uint8_t>(v);};
  std::vector<std::uint8_t> bytes(n,op(abc::Opcode::kNOP));
  bytes.insert(bytes.end(),{op(abc::Opcode::kASYNCFUNCTIONENTER),op(abc::Opcode::kSTA_V8),0,
    op(abc::Opcode::kLDUNDEFINED),op(abc::Opcode::kASYNCFUNCTIONRESOLVE_V8),0,
    op(abc::Opcode::kRETURN),op(abc::Opcode::kSTA_V8),1,
    op(abc::Opcode::kASYNCFUNCTIONREJECT_V8),0,op(abc::Opcode::kRETURN)});
  auto body=test::Body(bytes);abc::TryRegion region;region.offset={n+3};region.length=4;region.handler_count=1;
  abc::CatchHandler handler;handler.offset={n+7};handler.length=5;
  body.tries={region};body.handlers={handler};return body;
}
const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(3,1,{}));
AsyncCompletion Run(const abc::MethodBody& body,std::size_t bytes=SemanticBudget{}.bytes) {
  return ProveAsyncCompletion(body,frame,bytes,nullptr,nullptr,abc::ProfileId::kDynamic1206,ResourceFailureScope::kExcluded);
}
void RoundTrip(AsyncCompletionFailure error) {
  const auto route=FamilyRouteFailure(FamilyPrepareFailure{FamilyFileFailure{PhasedFileFailure{error}}});
  test::Check(route.async==error.async && route.pc==error.async.location.pc && route.method==error.async.location.method);
  test::Check(route.failure_class==(error.kind==FunctionEmitError::kResourceLimit?RouteFailureClass::kMemoryLimit:RouteFailureClass::kUnproved));
  test::Check(route.causes.entries[0].domain==FailureDomain<FunctionEmitError>());
  test::Check(route.causes.entries[1].domain==FailureDomain<AsyncCompletionCheck>());
  MethodRouteReceipt receipt;receipt.identity.file_size=4096;receipt.identity.file_hash.bytes[0]=1;
  receipt.identity.abc_version={12,0,6,0};receipt.identity.profile=abc::ProfileId::kDynamic1206;
  receipt.method={77};receipt.record={111};receipt.route=SourceRoute::kValueTree;
  receipt.stage=SourceStage::kValuesVerified;receipt.work_revision=0;receipt.coverage=Coverage::kUnsupported;receipt.failure=route;
  const auto serialized=SerializeRouteReceipt(receipt);
  test::Check(std::holds_alternative<std::string>(serialized));
  const auto parsed=ParseRouteReceipt(std::get<std::string>(serialized));
  test::Check(std::holds_alternative<MethodRouteReceipt>(parsed));
  test::Check(std::get<MethodRouteReceipt>(parsed).failure->async==route.async);
  nlohmann::json routed,recorded;tools::DescribeRecordFailure(RouteRefusal{route},routed);
  RecordProgramFailure entry{RecordProgramError::kFunction,route.pc};entry.async=route.async;entry.causes=route.causes;
  entry.method=route.method;entry.working_pc=route.working_pc;tools::DescribeRecordFailure(entry,recorded);
  test::Check(recorded["source_async"]==routed["source_failure"]["async"]);
  test::Check(recorded["source_causes"]==routed["source_failure"]["causes"]);
}
}
int main() {
  static_assert(std::is_trivially_copyable_v<AsyncCompletionEvidence>);
  static_assert(static_cast<unsigned>(FunctionEmitError::kKind)==1);
  static_assert(static_cast<unsigned>(FunctionEmitError::kResourceLimit)==6);
  test::Check(std::holds_alternative<abc::MethodBody>(Run(Body())));
  auto input=Body();input.tries.clear();
  test::Check(std::get<AsyncCompletionFailure>(Run(input)).async.check==AsyncCompletionCheck::kInput);
  input=Body();input.instructions[0].decoded.opcode=abc::Opcode::kASYNCFUNCTIONREJECT_V8;
  const auto zero=std::get<AsyncCompletionFailure>(Run(input));
  test::Check(zero.async.check==AsyncCompletionCheck::kPreludeEffects && zero.async.location.pc==abc::CodeOffset{0});
  test::Check(!zero.async.location.working_pc);RoundTrip(zero);
  input=Body();input.instructions[4].decoded.opcode=abc::Opcode::kNOP;
  test::Check(std::get<AsyncCompletionFailure>(Run(input)).async.check==AsyncCompletionCheck::kResolveReturn);
  input=Body();input.handlers.back().length=0;
  test::Check(std::get<AsyncCompletionFailure>(Run(input)).async.check==AsyncCompletionCheck::kCompletionCatch);
  // Actual lower-stage failures distinguish missing locations from real PC0.
  const auto raw_await = test::Body({static_cast<std::uint8_t>(abc::Opcode::kASYNCFUNCTIONAWAITUNCAUGHT_V8), 0});
  const auto budget = std::get<AwaitRegionFailure>(FindAwaitRegions(raw_await, abc::ProfileId::kDynamic1206, 4, 0));
  test::Check(budget.kind == AwaitRegionError::kResourceLimit && !budget.pc);
  const auto shape = std::get<AwaitRegionFailure>(FindAwaitRegions(raw_await, abc::ProfileId::kDynamic1206, 4, SemanticBudget{}.bytes));
  test::Check(shape.kind == AwaitRegionError::kUnsupportedShape && shape.pc == abc::CodeOffset{0});
  const std::array<AwaitRegion, 1> certificates{};
  const auto catch_budget = std::get<AwaitCatchFailure>(ProveAwaitCatchInputs(Body(), certificates,
      abc::ProfileId::kDynamic1206, 4, {7}, {0}));
  test::Check(catch_budget.kind == AwaitCatchError::kResourceLimit && !catch_budget.pc);
  const auto normalized_budget = std::get<AsyncCompletionFailure>(Run(Body(), 0));
  test::Check(normalized_budget.kind == FunctionEmitError::kResourceLimit && !normalized_budget.async.location.pc);
  RoundTrip(normalized_budget);
  // A real normalized boundary has distinct original/working coordinates.
  auto original = Body();
  auto normalized = std::get<abc::MethodBody>(Run(original));
  const std::array<abc::CodeOffset, 2> origins{{{3}, {6}}};
  auto user_try = original.tries.back(); user_try.offset = {3}; user_try.length = 2;
  original.tries.insert(original.tries.begin(), user_try);
  AsyncCompletionEvidence boundary;
  test::Check(RebaseAsyncHandlers(original, origins, normalized, SemanticBudget{}.bytes, &boundary) == FunctionEmitError::kKind);
  test::Check(boundary.check == AsyncCompletionCheck::kTryBoundary && boundary.location.pc == abc::CodeOffset{3}
      && boundary.location.working_pc == abc::CodeOffset{0});
  original = Body(); normalized = std::get<abc::MethodBody>(Run(original));
  auto user_handler = original.handlers.back(); user_handler.offset = {3}; user_handler.length = 2;
  original.handlers.insert(original.handlers.begin(), user_handler);
  test::Check(RebaseAsyncHandlers(original, origins, normalized, SemanticBudget{}.bytes, &boundary) == FunctionEmitError::kKind);
  test::Check(boundary.check == AsyncCompletionCheck::kHandlerBoundary && boundary.location.pc == abc::CodeOffset{3}
      && boundary.location.working_pc == abc::CodeOffset{0});
  const auto op = [](abc::Opcode value) { return static_cast<std::uint8_t>(value); };
  auto branch = test::Body({op(abc::Opcode::kASYNCFUNCTIONENTER), op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDUNDEFINED), op(abc::Opcode::kJMP_IMM8), 4,
      op(abc::Opcode::kASYNCFUNCTIONRESOLVE_V8), 0, op(abc::Opcode::kRETURN),
      op(abc::Opcode::kSTA_V8), 1, op(abc::Opcode::kASYNCFUNCTIONREJECT_V8), 0, op(abc::Opcode::kRETURN)});
  branch.tries = Body().tries; branch.tries[0].length = 6;
  branch.handlers = Body().handlers; branch.handlers[0].offset = {9};
  const auto rebased = std::get<AsyncCompletionFailure>(Run(branch));
  test::Check(rebased.async.check == AsyncCompletionCheck::kBranchRebase
      && rebased.async.location.pc == abc::CodeOffset{4}
      && rebased.async.location.working_pc == abc::CodeOffset{1}
      && rebased.async.related_pc == abc::CodeOffset{8});
  RoundTrip(rebased);
  for(unsigned tag=0;tag<=static_cast<unsigned>(AsyncCompletionCheck::kBudget);++tag){
    AsyncCompletionFailure e{static_cast<AsyncCompletionCheck>(tag)};
    RoundTrip(e);e.async.location.method=abc::EntityOffset{77};e.async.location.pc=abc::CodeOffset{0};
    e.async.location.working_pc=abc::CodeOffset{13};e.async.related_pc=abc::CodeOffset{21};
    e.async.catch_input=AwaitCatchObservation{{21},{0},CatchInputPrefix::kUnprovedPaths,false,true};
    RoundTrip(e);e.cause=AwaitCatchError::kObservedRegister;RoundTrip(e);
    e.cause=AwaitRegionError::kInteriorEntry;RoundTrip(e);
    auto j=nlohmann::json::parse(AsyncCompletionJson(e.async).dump());test::Check(ParseAsyncCompletion(j)==e.async);
    j["check"]=UINT32_MAX;test::Check(!ParseAsyncCompletion(j));
    j=AsyncCompletionJson(e.async);j["catch_input"]["register"]=-1;test::Check(!ParseAsyncCompletion(j));
  }
  for(unsigned n:{200U,201U,400U,800U}) {
    const auto body=Body(n);std::size_t lo=0,hi=SemanticBudget{}.bytes;
    while(lo<hi){const auto mid=lo+(hi-lo)/2;if(std::holds_alternative<abc::MethodBody>(Run(body,mid)))hi=mid;else lo=mid+1;}
    test::Check(lo && std::holds_alternative<abc::MethodBody>(Run(body,lo)));
    const auto limited=std::get<AsyncCompletionFailure>(Run(body,lo-1));
    test::Check(limited.kind==FunctionEmitError::kResourceLimit);RoundTrip(limited);
    for(unsigned i=0;i<n;++i){AsyncCompletionFailure e{AsyncCompletionCheck::kCatchInput,abc::CodeOffset{i}};RoundTrip(e);}
    std::cout<<"async evidence n="<<n<<" exact bytes="<<lo<<'\n';
  }
}
