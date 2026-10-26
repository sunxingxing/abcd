#include "abcd/analysis/exception_throw_domain.h"
#include "abcd/analysis/exception_inputs.h"
#include "analysis/ssa_fixture.h"
#include "abcd/corpus/abc_file.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
constexpr auto kProfile = abc::ProfileId::kDynamic1301;
constexpr auto Byte(abc::Opcode op) { return static_cast<std::uint8_t>(op); }

abc::MethodBody Body(std::uint32_t width, bool inverse = false, bool strict = false) {
  std::vector<std::uint8_t> bytes;
  // A branch-selected Hole/language payload, then copies before testing and
  // throwing. The test's normal edge is the only path to the explicit throw.
  for (std::uint32_t i = 0; i < width; ++i) bytes.push_back(Byte(abc::Opcode::kLDUNDEFINED));
  const auto entry = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {Byte(abc::Opcode::kLDA_V8), 3, Byte(abc::Opcode::kJEQZ_IMM8), 6,
      Byte(abc::Opcode::kLDA_V8), 4, Byte(abc::Opcode::kJMP_IMM8), 3,
      Byte(abc::Opcode::kLDHOLE), Byte(abc::Opcode::kSTA_V8), 0,
      Byte(abc::Opcode::kLDHOLE), Byte(abc::Opcode::kSTA_V8), 1,
      Byte(abc::Opcode::kLDA_V8), 0,
      Byte(inverse ? (strict ? abc::Opcode::kSTRICTEQ_IMM8_V8 : abc::Opcode::kEQ_IMM8_V8)
                   : (strict ? abc::Opcode::kSTRICTNOTEQ_IMM8_V8 : abc::Opcode::kNOTEQ_IMM8_V8)), 0, 1,
      Byte(inverse ? abc::Opcode::kJNEZ_IMM8 : abc::Opcode::kJEQZ_IMM8), 6,
      Byte(abc::Opcode::kLDA_V8), 0, 0xfe, 0,
      Byte(abc::Opcode::kRETURNUNDEFINED), Byte(abc::Opcode::kRETURN)});
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {entry + 23}; region.length = 2; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {entry + 26}; handler.length = 1;
  body.tries.push_back(region); body.handlers.push_back(handler);
  return body;
}

void Check(std::uint32_t width, bool inverse, bool strict) {
  const auto body = Body(width, inverse, strict);
  const auto built = BuildExceptionFlow(body, kProfile, 5);
  test::Check(std::holds_alternative<ExceptionFlow>(built));
  const auto& flow = std::get<ExceptionFlow>(built);
  IdfQuota q{kDefaultSemanticWork}; auto renamed = ExceptionSsa::Build(flow, 5, q);
  test::Check(std::holds_alternative<ExceptionSsa>(renamed));
  const auto& ssa = std::get<ExceptionSsa>(renamed);
  RawEventArena arena{{}}; const auto proof = DeriveExceptionThrowDomains(flow, ssa, arena);
  test::Check(proof.size() == 1);
  RawEventArena verify{{}}; VerifyExceptionThrowDomains(flow, ssa, proof, verify);
  RawEventArena exact_verify{{verify.Peak(), verify.Work()}};
  VerifyExceptionThrowDomains(flow, ssa, proof, exact_verify);
  for (const bool work : {false, true}) {
    RawEventArena low{{work ? verify.Peak() : verify.Peak()-1,
        work ? verify.Work()-1 : verify.Work()}};
    bool refused = false;
    try { VerifyExceptionThrowDomains(flow, ssa, proof, low); }
    catch (const RawEventFailure&) { refused = true; } catch (const std::bad_alloc&) { refused = true; }
    test::Check(refused);
  }
  // Exact work and byte boundaries, including both independent computations.
  RawEventArena exact{{arena.Peak(), arena.Work()}};
  test::Check(DeriveExceptionThrowDomains(flow, ssa, exact) == proof);
  for (const bool work : {false, true}) {
    RawEventArena short_budget{{work ? arena.Peak() : arena.Peak() - 1,
        work ? arena.Work() - 1 : arena.Work()}};
    bool failed = false;
    try { (void)DeriveExceptionThrowDomains(flow, ssa, short_budget); }
    catch (const RawEventFailure&) { failed = true; } catch (const std::bad_alloc&) { failed = true; }
    test::Check(failed);
  }
  for (unsigned mutation = 0; mutation < 5; ++mutation) {
    std::vector<ExceptionThrowDomain> changed(proof.begin(), proof.end());
    switch (mutation) {
      case 0: changed.front().payload.value ^= 1; break;
      case 1: changed.front().successor ^= 1; break;
      case 2: changed.front().branch.value ^= 1; break;
      case 3: changed.clear(); break;
      case 4: changed.push_back(changed.front()); break;
    }
    RawEventArena check{{}}; bool rejected = false;
    try { VerifyExceptionThrowDomains(flow, ssa, changed, check); }
    catch (const RawEventFailure&) { rejected = true; }
    test::Check(rejected);
  }
  std::vector<EntryValueFact> entries;
  for (std::uint32_t v = 0; v < ssa.Values().size(); ++v)
    if (std::holds_alternative<MethodEntry>(ssa.Values()[v].origin))
      if (const auto* reg = std::get_if<abc::RegisterId>(&ssa.Values()[v].slot); reg && reg->value >= 3)
        entries.push_back({{v}, ValueKinds::Language()});
  const auto solve = [&](ExceptionThrowRefinement refinement, auto input) {
    IdfQuota work{kDefaultSemanticWork};
    auto result = ExceptionValueFlow::Build(flow, ssa, input, work, {}, PendingExceptionValues::kLanguage,
        {}, {}, CallResultValues::kLanguage, FrameReceiver::kUnknown,
        ArithmeticSemantics::kPinnedRuntime, refinement);
    test::Check(std::holds_alternative<ExceptionValueFlow>(result));
    return std::get<ExceptionValueFlow>(std::move(result));
  };
  const auto ordinary = solve(ExceptionThrowRefinement::kNone, entries);
  const auto refined = solve(ExceptionThrowRefinement::kGuardedNonHole, entries);
  for (std::uint32_t n = 0; n < flow.nodes.size(); ++n)
    if (flow.nodes[n].phase == ExceptionPhase::kCatch) {
      const auto caught = *ssa.Write({n});
      test::Check(ordinary.Facts()[caught.value].Contains(VmValueKind::kHole));
      test::Check(refined.Facts()[caught.value].IsLanguage());
      // Exclusion must never turn other internal values into language values.
      for (auto& entry : entries) entry.kinds = ValueKinds::Unknown();
      const auto unknown = solve(ExceptionThrowRefinement::kGuardedNonHole, entries);
      test::Check(unknown.Facts()[caught.value].Contains(VmValueKind::kEnvironment));
      test::Check(!unknown.Facts()[caught.value].Contains(VmValueKind::kHole));
    }
  // An alternate edge entering the throw bypasses the comparison. Edge
  // dominance must fail, even if the original successor still reaches it.
  auto external = flow;
  for (auto& node : external.nodes) if (node.phase == ExceptionPhase::kBefore &&
      external.instructions[node.instruction.value].pc.value == width + 2)
    node.successors[0].target = proof.front().site;
  RawEventArena changed{{}};
  auto other_ssa = ExceptionSsa::Build(external, 5, q);
  test::Check(std::holds_alternative<ExceptionSsa>(other_ssa));
  test::Check(DeriveExceptionThrowDomains(external, std::get<ExceptionSsa>(other_ssa), changed).empty());
}
void Unrefined() {
  for (const bool dropped : {false, true}) {
    auto body = Body(0);
    for (auto& i : body.instructions) if (i.offset.value == 19) {
      const auto replacement = test::Body({Byte(dropped ? abc::Opcode::kJMP_IMM8 : abc::Opcode::kJNEZ_IMM8), 6}).instructions.front();
      i.decoded = replacement.decoded;
    }
    const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, kProfile, 5));
    IdfQuota q{kDefaultSemanticWork};
    const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, 5, q));
    RawEventArena arena{{}}; test::Check(DeriveExceptionThrowDomains(flow, ssa, arena).empty());
  }
  for (const bool cycle : {false, true}) {
    auto body = Body(0);
    if (!cycle) for (auto& instruction : body.instructions)
      if (instruction.offset.value == 21) instruction.decoded.operands[0].raw_bits = 1;
    auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, kProfile, 5));
    if (cycle) {
      std::optional<ExceptionNodeId> join;
      for (std::uint32_t n = 0; n < flow.nodes.size(); ++n)
        if (flow.nodes[n].phase == ExceptionPhase::kBefore && flow.instructions[flow.nodes[n].instruction.value].pc.value == 9) join = ExceptionNodeId{n};
      test::Check(join.has_value());
      for (auto& node : flow.nodes) if (node.phase == ExceptionPhase::kCommit && flow.instructions[node.instruction.value].pc.value == 25) {
        flow.instructions[node.instruction.value].operation = JumpOperation{{9}};
        node.successor_count = 1; node.successors[0] = {*join, ExceptionArcKind::kTaken};
      }
    }
    IdfQuota q{kDefaultSemanticWork};
    const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, 5, q));
    RawEventArena arena{{}};
    test::Check(DeriveExceptionThrowDomains(flow, ssa, arena).empty());
    if (!cycle) {
      const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, {}, q, {},
          PendingExceptionValues::kLanguage, {}, {}, CallResultValues::kLanguage, FrameReceiver::kUnknown,
          ArithmeticSemantics::kPinnedRuntime, ExceptionThrowRefinement::kGuardedNonHole));
      for (std::uint32_t n = 0; n < flow.nodes.size(); ++n) if (flow.nodes[n].phase == ExceptionPhase::kCatch)
        test::Check(values.Facts()[ssa.Write({n})->value].Contains(VmValueKind::kHole));
    }
  }
}

void UnguardedHole() {
  auto body=test::Body({Byte(abc::Opcode::kLDHOLE),0xfe,0,Byte(abc::Opcode::kRETURN)});
  abc::TryRegion region;region.offset={1};region.length=2;region.handler_count=1;
  abc::CatchHandler handler;handler.offset={3};handler.length=1;
  body.tries.push_back(region);body.handlers.push_back(handler);
  const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(body,kProfile,0));
  IdfQuota quota{kDefaultSemanticWork};const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,0,quota));
  auto values=std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,{},quota,{},PendingExceptionValues::kLanguage,{}, {},
      CallResultValues::kLanguage,FrameReceiver::kUnknown,ArithmeticSemantics::kPinnedRuntime,ExceptionThrowRefinement::kGuardedCatchPayload));
  test::Check(values.GuardedPayloads() && values.GuardedPayloads()->proof.constraints.empty());
  const auto env=std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  const auto checked=VerifyExceptionInputs(flow,ssa,env,values);
  const auto* refused=std::get_if<ExceptionInputFailure>(&checked);
  test::Check(refused && refused->kind==ExceptionInputError::kInternalValue && refused->pc.value==1 && !refused->throw_domain);
}
void CatchTransport() {
  auto body=Body(0);
  std::vector<std::uint8_t> bytes;
  // Keep the original guarded throw and replace its handler with catch/copy/
  // rethrow transport into a second Catch. The second payload has a distinct
  // Catch definition, so canonical-copy identity alone cannot prove it.
  bytes={Byte(abc::Opcode::kLDA_V8),3,Byte(abc::Opcode::kJEQZ_IMM8),6,
      Byte(abc::Opcode::kLDA_V8),4,Byte(abc::Opcode::kJMP_IMM8),3,
      Byte(abc::Opcode::kLDHOLE),Byte(abc::Opcode::kSTA_V8),0,
      Byte(abc::Opcode::kLDHOLE),Byte(abc::Opcode::kSTA_V8),1,
      Byte(abc::Opcode::kLDA_V8),0,Byte(abc::Opcode::kNOTEQ_IMM8_V8),0,1,
      Byte(abc::Opcode::kJEQZ_IMM8),6,Byte(abc::Opcode::kLDA_V8),0,0xfe,0,
      Byte(abc::Opcode::kRETURNUNDEFINED)};
  bytes.insert(bytes.end(),{Byte(abc::Opcode::kSTA_V8),2,Byte(abc::Opcode::kLDA_V8),2,0xfe,0,Byte(abc::Opcode::kRETURN)});
  auto expanded=test::Body(bytes);expanded.tries=body.tries;expanded.handlers=body.handlers;
  expanded.handlers[0].length=6;
  abc::TryRegion outer;outer.offset={26};outer.length=6;outer.first_handler=1;outer.handler_count=1;
  abc::CatchHandler handler;handler.offset={32};handler.length=1;
  expanded.tries.push_back(outer);expanded.handlers.push_back(handler);
  const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(expanded,kProfile,5));
  IdfQuota quota{kDefaultSemanticWork};const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,5,quota));
  RawEventArena arena{{}};const auto proof=DeriveGuardedCatchPayloads(flow,ssa,arena);
  test::Check(proof.throws.size()==1 && proof.constraints.size()==2);
  RawEventArena verify{{}};VerifyGuardedCatchPayloads(flow,ssa,proof,verify);
}
void PhiTransport() {
  for(bool direct_edge:{false,true}) {
  std::vector<std::uint8_t> bytes;
  const auto emit=[&](std::initializer_list<std::uint8_t> code){const auto at=static_cast<std::uint32_t>(bytes.size());bytes.insert(bytes.end(),code);return at;};
  const auto jump=[&](std::uint32_t at,std::uint32_t to){test::Check(to>=at && to-at<128);bytes[at+1]=static_cast<std::uint8_t>(to-at);};
  emit({Byte(abc::Opcode::kLDA_V8),3});const auto choose=emit({Byte(abc::Opcode::kJEQZ_IMM8),0});
  const auto arm=[&](std::uint8_t reg){
    emit({Byte(abc::Opcode::kLDA_V8),reg,Byte(abc::Opcode::kSTA_V8),0,
      Byte(abc::Opcode::kLDHOLE),Byte(abc::Opcode::kSTA_V8),1,Byte(abc::Opcode::kLDA_V8),0,
      Byte(abc::Opcode::kNOTEQ_IMM8_V8),0,1});
    if(direct_edge) {
      const auto join=emit({Byte(abc::Opcode::kJNEZ_IMM8),0});
      emit({Byte(abc::Opcode::kRETURNUNDEFINED)});return std::pair{join,join};
    }
    const auto done=emit({Byte(abc::Opcode::kJEQZ_IMM8),0});
    const auto join=emit({Byte(abc::Opcode::kJMP_IMM8),0});return std::pair{done,join};
  };
  const auto left=arm(4);const auto right_pc=static_cast<std::uint32_t>(bytes.size());const auto right=arm(5);
  const auto join=emit({Byte(abc::Opcode::kLDA_V8),0});const auto thrown=emit({0xfe,0});
  const auto done=emit({Byte(abc::Opcode::kRETURNUNDEFINED)});const auto caught=emit({Byte(abc::Opcode::kRETURN)});
  jump(choose,right_pc);if(!direct_edge){jump(left.first,done);jump(right.first,done);}jump(left.second,join);jump(right.second,join);
  auto body=test::Body(bytes);abc::TryRegion region;region.offset={thrown};region.length=2;region.handler_count=1;
  abc::CatchHandler handler;handler.offset={caught};handler.length=1;body.tries.push_back(region);body.handlers.push_back(handler);
  const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(body,kProfile,6));
  IdfQuota quota{kDefaultSemanticWork};const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,6,quota));
  RawEventArena arena{{}};const auto proof=DeriveGuardedCatchPayloads(flow,ssa,arena);
  test::Check(proof.throws.empty() && proof.constraints.size()==1);
  RawEventArena verify{{}};VerifyGuardedCatchPayloads(flow,ssa,proof,verify);
  std::vector<EntryValueFact> entries;
  for(std::uint32_t v=0;v<ssa.Values().size();++v)if(std::holds_alternative<MethodEntry>(ssa.Values()[v].origin))
    if(const auto* reg=std::get_if<abc::RegisterId>(&ssa.Values()[v].slot);reg && reg->value>=3)entries.push_back({{v},ValueKinds::Language().Join(ValueKinds::Hole())});
  auto values=std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,entries,quota,{},PendingExceptionValues::kLanguage,{}, {},
      CallResultValues::kLanguage,FrameReceiver::kUnknown,ArithmeticSemantics::kPinnedRuntime,ExceptionThrowRefinement::kGuardedCatchPayload));
  for(std::uint32_t n=0;n<flow.nodes.size();++n)if(flow.nodes[n].phase==ExceptionPhase::kCatch)
    test::Check(values.Facts()[ssa.Write({n})->value].IsLanguage());
  test::Check(values.Facts()[proof.constraints[0].payload.value].Contains(VmValueKind::kHole));
  }
}
void CurrentActivation() {
  for (bool loop : {false, true}) {
    auto body = Body(0);
    auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, kProfile, 5));
    if (loop) {
      std::optional<ExceptionNodeId> join;
      for (std::uint32_t n=0;n<flow.nodes.size();++n)
        if(flow.nodes[n].phase==ExceptionPhase::kBefore && flow.instructions[flow.nodes[n].instruction.value].pc.value==9) join=ExceptionNodeId{n};
      test::Check(join.has_value());
      for(auto& node:flow.nodes) if(node.phase==ExceptionPhase::kCommit && flow.instructions[node.instruction.value].pc.value==25) {
        flow.instructions[node.instruction.value].operation=JumpOperation{{9}};
        node.successor_count=1;node.successors[0]={*join,ExceptionArcKind::kTaken};
      }
    }
    IdfQuota quota{kDefaultSemanticWork};
    auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,5,quota));
    RawEventArena arena{{}};auto proof=DeriveGuardedCatchPayloads(flow,ssa,arena);
    test::Check(proof.throws.size()==1 && proof.throws[0].definition.has_value());
    RawEventArena verify{{}};VerifyGuardedCatchPayloads(flow,ssa,proof,verify);
    for(unsigned mutation=0;mutation<6;++mutation) {
      auto changed=proof;
      auto expected=ExceptionThrowDomainError::kGuard;
      switch(mutation) {
        case 0: changed.throws[0].successor^=1;break;
        case 1: changed.definitions[changed.throws[0].compared.value].site=ExceptionNodeId{0};expected=ExceptionThrowDomainError::kActivation;break;
        case 2: test::Check(!changed.phis.empty());changed.phis[0].source=ExceptionNodeId{0};expected=ExceptionThrowDomainError::kPhi;break;
        case 3: changed.catches.clear();expected=ExceptionThrowDomainError::kCoverage;break;
        case 4: changed.catches[0].payload=SsaValueId{0};expected=ExceptionThrowDomainError::kCoverage;break;
        case 5: changed.reads[0].value.value^=1;expected=ExceptionThrowDomainError::kActivation;break;
      }
      RawEventArena check{{}}; bool refused=false;
      try { VerifyGuardedCatchPayloads(flow,ssa,changed,check); }
      catch(const ExceptionThrowDomainFailure& failure){test::Check(failure.kind==expected);refused=true;}
      test::Check(refused);
    }
    std::vector<EntryValueFact> entries;
    for(std::uint32_t v=0;v<ssa.Values().size();++v)if(std::holds_alternative<MethodEntry>(ssa.Values()[v].origin))
      if(const auto* reg=std::get_if<abc::RegisterId>(&ssa.Values()[v].slot);reg && reg->value>=3)entries.push_back({{v},ValueKinds::Language()});
    auto values=std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,entries,quota,{},PendingExceptionValues::kLanguage,{}, {},
        CallResultValues::kLanguage,FrameReceiver::kUnknown,ArithmeticSemantics::kPinnedRuntime,ExceptionThrowRefinement::kGuardedCatchPayload));
    for(std::uint32_t n=0;n<flow.nodes.size();++n)if(flow.nodes[n].phase==ExceptionPhase::kCatch)
      test::Check(values.Facts()[ssa.Write({n})->value].IsLanguage());
    // The payload itself keeps its Hole alternative. Only the dispatch edge is filtered.
    test::Check(values.Facts()[proof.throws[0].payload.value].Contains(VmValueKind::kHole));
  }
  // A guarded iteration can replace the payload on another path before
  // throwing. SSA inserts a phi; the old comparison cannot filter that phi.
  {
    auto body=test::Body({Byte(abc::Opcode::kLDHOLE),Byte(abc::Opcode::kSTA_V8),1,
      Byte(abc::Opcode::kLDA_V8),3,Byte(abc::Opcode::kSTA_V8),0,
      Byte(abc::Opcode::kLDA_V8),0,Byte(abc::Opcode::kNOTEQ_IMM8_V8),0,1,
      Byte(abc::Opcode::kJEQZ_IMM8),15,
      Byte(abc::Opcode::kLDA_V8),4,Byte(abc::Opcode::kJEQZ_IMM8),5,
      Byte(abc::Opcode::kLDHOLE),Byte(abc::Opcode::kSTA_V8),0,
      Byte(abc::Opcode::kLDA_V8),0,0xfe,0,
      Byte(abc::Opcode::kJMP_IMM8),static_cast<std::uint8_t>(-22),
      Byte(abc::Opcode::kRETURN)});
    abc::TryRegion region;region.offset={23};region.length=2;region.handler_count=1;
    abc::CatchHandler handler;handler.offset={27};handler.length=1;
    body.tries.push_back(region);body.handlers.push_back(handler);
    const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(body,kProfile,5));
    IdfQuota quota{kDefaultSemanticWork};const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,5,quota));
    RawEventArena arena{{}};test::Check(DeriveGuardedCatchPayloads(flow,ssa,arena).throws.empty());
    auto values=std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,{},quota,{},PendingExceptionValues::kLanguage,{}, {},
        CallResultValues::kLanguage,FrameReceiver::kUnknown,ArithmeticSemantics::kPinnedRuntime,ExceptionThrowRefinement::kGuardedCatchPayload));
    for(std::uint32_t n=0;n<flow.nodes.size();++n)if(flow.nodes[n].phase==ExceptionPhase::kCatch)
      test::Check(values.Facts()[ssa.Write({n})->value].Contains(VmValueKind::kHole));
  }
  // Wrong branch direction and a real unguarded Hole payload cannot acquire a constraint.
  for(bool wrong_direction:{false,true}) {
    auto body=Body(0);
    for(auto& instruction:body.instructions) {
      if(wrong_direction && instruction.offset.value==19)
        instruction.decoded=test::Body({Byte(abc::Opcode::kJNEZ_IMM8),6}).instructions[0].decoded;
      if(!wrong_direction && instruction.offset.value==21)instruction.decoded.operands[0].raw_bits=1;
    }
    const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(body,kProfile,5));
    IdfQuota quota{kDefaultSemanticWork};const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,5,quota));
    RawEventArena arena{{}};test::Check(DeriveGuardedCatchPayloads(flow,ssa,arena).throws.empty());
  }
}

void Width(std::uint32_t count) {
  std::vector<std::uint8_t> bytes;
  const auto one = Body(0);
  // Serialized fixture only: many independent guards sharing a caller frame.
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::uint8_t segment[] = {Byte(abc::Opcode::kLDA_V8), 3, Byte(abc::Opcode::kJEQZ_IMM8), 6,
        Byte(abc::Opcode::kLDA_V8), 4, Byte(abc::Opcode::kJMP_IMM8), 3,
        Byte(abc::Opcode::kLDHOLE), Byte(abc::Opcode::kSTA_V8), 0,
        Byte(abc::Opcode::kLDHOLE), Byte(abc::Opcode::kSTA_V8), 1,
        Byte(abc::Opcode::kLDA_V8), 0, Byte(abc::Opcode::kNOTEQ_IMM8_V8), 0, 1,
        Byte(abc::Opcode::kJEQZ_IMM8), 6, Byte(abc::Opcode::kLDA_V8), 0, 0xfe, 0,
        Byte(abc::Opcode::kJMP_IMM32), 6, 0, 0, 0, Byte(abc::Opcode::kRETURN)};
    bytes.insert(bytes.end(), std::begin(segment), std::end(segment));
  }
  bytes.push_back(Byte(abc::Opcode::kRETURNUNDEFINED));
  auto body = test::Body(bytes);
  for (std::uint32_t i = 0; i < count; ++i) {
    auto region = one.tries.front(); region.offset.value += 31 * i; region.first_handler = i;
    auto handler = one.handlers.front(); handler.offset.value = 31 * i + 30;
    body.tries.push_back(region); body.handlers.push_back(handler);
  }
  auto built = BuildExceptionFlow(body, kProfile, 5);
  if (!std::holds_alternative<ExceptionFlow>(built)) {
    std::cerr << "width flow failure=" << built.index() << " count=" << count << '\n';
    if(const auto* f=std::get_if<SemanticFailure>(&built))std::cerr<<"semantic="<<unsigned(f->kind)<<" pc="<<f->pc.value<<'\n';
    test::Check(false);
  }
  const auto flow = std::get<ExceptionFlow>(std::move(built));
  IdfQuota q{kDefaultSemanticWork};
  auto renamed=ExceptionSsa::Build(flow, 5, q);
  if(!std::holds_alternative<ExceptionSsa>(renamed))std::cerr<<"width ssa failure "<<renamed.index()<<" count="<<count<<'\n';
  const auto ssa = std::get<ExceptionSsa>(std::move(renamed));
  RawEventArena arena{{}};
  const auto proof = DeriveExceptionThrowDomains(flow, ssa, arena);
  test::Check(proof.size() == count);
  RawEventArena verify{{}}; VerifyExceptionThrowDomains(flow, ssa, proof, verify);
  RawEventArena exact{{arena.Peak(), arena.Work()}};
  test::Check(DeriveExceptionThrowDomains(flow, ssa, exact) == proof);
  for (bool work : {false, true}) {
    RawEventArena low{{work ? arena.Peak() : arena.Peak()-1, work ? arena.Work()-1 : arena.Work()}};
    bool failed = false;
    try { (void)DeriveExceptionThrowDomains(flow, ssa, low); }
    catch (const RawEventFailure&) { failed = true; } catch (const std::bad_alloc&) { failed = true; }
    test::Check(failed);
  }
  RawEventArena current{{}};const auto extended=DeriveGuardedCatchPayloads(flow,ssa,current);
  test::Check(extended.constraints.size()==count);
  RawEventArena current_verify{{}};VerifyGuardedCatchPayloads(flow,ssa,extended,current_verify);
  RawEventArena exact_current{{current.Peak(),current.Work()}};
  const auto repeated=DeriveGuardedCatchPayloads(flow,ssa,exact_current);
  test::Check(repeated.constraints==extended.constraints);
  for(bool work:{false,true}) {
    RawEventArena low{{work?current.Peak():current.Peak()-1,work?current.Work()-1:current.Work()}};
    bool refused=false;
    try{(void)DeriveGuardedCatchPayloads(flow,ssa,low);}
    catch(const RawEventFailure&){refused=true;}
    catch(const ExceptionThrowDomainFailure&){refused=true;}
    catch(const std::bad_alloc&){refused=true;}
    test::Check(refused);
  }
  std::cout << "width=" << count << " work=" << arena.Work() << " bytes=" << arena.Peak() << " current-work=" << current.Work() << " current-bytes=" << current.Peak() << '\n';
}
void Fixture(const char* path) {
  const auto file = std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  const auto hash = std::get<corpus::FileFingerprint>(file->Fingerprint());
  const auto header = std::get<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size), hash.size));
  const auto index = std::get<abc::MetadataIndex>(abc::BuildMetadataIndex(*file, header));
  unsigned refined = 0;
  for (const auto& method : index.methods) {
    if (!method.code) continue;
    const auto body = std::get<abc::MethodBody>(abc::DecodeMethodBody(*file, header.profile, *method.code));
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(method.code->registers, method.code->arguments, {}));
    const auto built = BuildExceptionFlow(body, header.profile, frame.registers, {}, {}, &frame);
    if (!std::holds_alternative<ExceptionFlow>(built)) continue;
    const auto& flow = std::get<ExceptionFlow>(built);
    IdfQuota q{kDefaultSemanticWork}; const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, q));
    RawEventArena arena{{}}; const auto proof = DeriveExceptionThrowDomains(flow, ssa, arena);
    if (proof.empty()) continue;
    RawEventArena verify{{}}; VerifyExceptionThrowDomains(flow, ssa, proof, verify);
    refined += proof.size();
    std::vector<EntryValueFact> entries;
    for (std::uint32_t v = 0; v < ssa.Values().size(); ++v)
      if (std::holds_alternative<MethodEntry>(ssa.Values()[v].origin))
        if (const auto* r = std::get_if<abc::RegisterId>(&ssa.Values()[v].slot); r && r->value >= method.code->registers)
          entries.push_back({{v}, ValueKinds::Language()});
    const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries, q, {},
        PendingExceptionValues::kLanguage, {}, {}, CallResultValues::kLanguage, FrameReceiver::kInitialized,
        ArithmeticSemantics::kSourceNumeric, ExceptionThrowRefinement::kGuardedNonHole));
    const auto env = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
    test::Check(std::holds_alternative<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, env, values,
        kDefaultSemanticWork, nullptr, ExceptionInputDomain::kHoleTransport)));
    std::cout << "fixture method=" << method.offset.value << " refinements=" << proof.size() << '\n';
  }
  test::Check(refined != 0);
}

}
int main(int argc, char** argv) {
  for (auto n : {0u, 200u, 201u, 400u, 800u}) for (bool inverse : {false, true}) for (bool strict : {false, true}) Check(n, inverse, strict);
  Unrefined();
  CurrentActivation();
  PhiTransport();
  CatchTransport();
  UnguardedHole();
  for (auto n : {200u, 201u, 400u, 800u}) Width(n);
  test::Check(argc == 2); Fixture(argv[1]);
}
