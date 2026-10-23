#include "abcd/analysis/catch_payload_domain.h"
#include <algorithm>
#include <limits>
namespace abcd::analysis {
namespace {
bool SameSlot(const ScalarSlot& a,const ScalarSlot& b) {
  if(std::holds_alternative<Accumulator>(a))return std::holds_alternative<Accumulator>(b);
  const auto* reg=std::get_if<abc::RegisterId>(&b);
  return reg && std::get<abc::RegisterId>(a)==*reg;
}
// OH7 f04900cf: runtime_stubs-inl.h RuntimeThrow passes raw acc unchanged.
// ObjectCheck/HoleGuard/SuperCheck construct runtime errors; user-code effects
// are external boundaries covered ONLY by the 2026-10-16 NoHoleV1 premise.
bool RuntimeError(const SemanticOperation& op) {
  return std::holds_alternative<ObjectCheck>(op) || std::holds_alternative<HoleGuard>(op) ||
      std::holds_alternative<SuperCheck>(op) ||
      (std::get_if<ThrowOperation>(&op)&&ThrowsIntrinsicTypeError(std::get<ThrowOperation>(op).kind));
}
bool External(const SemanticInstruction& instruction) {
  // The shared effect model owns which instructions may execute user code,
  // including conversions/setters, rather than a recovery opcode whitelist.
  const auto effects=instruction.effects.normal | instruction.effects.before_throw;
  return (static_cast<std::uint16_t>(effects) & static_cast<std::uint16_t>(LanguageEffect::kUserCode))!=0;
}
}
std::pmr::vector<CatchPayloadFact> DeriveCatchPayloadDomains(const ExceptionFlow& flow,
    const ExceptionSsa& ssa, ExternalExceptionPolicy policy, RawEventArena& arena) {
  const auto fail=[](){throw RawEventFailure{RawEventError::kCorrespondence};};
  const auto nv=ssa.Values().size(), nn=flow.nodes.size();
  if(nv>UINT32_MAX || nn>UINT32_MAX-nv)throw RawEventFailure{RawEventError::kResourceLimit};
  const auto count=static_cast<std::uint32_t>(nv+nn);
  struct Dependency {std::uint32_t source,target;auto operator<=>(const Dependency&) const=default;};
  auto* memory=arena.Resource();
  std::pmr::vector<Dependency> dependencies(memory);
  std::pmr::vector<std::uint32_t> needed(count,0,memory), offsets(count+std::size_t{1},0,memory),queue(memory);
  std::pmr::vector<std::uint8_t> blocked(count,0,memory),known(count,0,memory);
  const auto add=[&](std::uint32_t target,std::optional<std::uint32_t> source) {
    arena.Step();if(!source){blocked[target]=1;return;}
    if(*source>=count)fail();
    if(needed[target]==UINT32_MAX)throw RawEventFailure{RawEventError::kResourceLimit};
    ++needed[target];dependencies.push_back({*source,target});
  };
  const auto scalar=[&](std::uint32_t target,const ScalarValue& value,ExceptionNodeId node) {
    if(std::holds_alternative<IntegerLiteral>(value) || std::holds_alternative<SpecialValue>(value) ||
        std::holds_alternative<Float64Bits>(value) || std::holds_alternative<EmptyArray>(value))return;
    std::optional<ScalarSlot> slot;
    if(const auto* reg=std::get_if<abc::RegisterId>(&value))slot=*reg;
    else if(std::holds_alternative<Accumulator>(value))slot=Accumulator{};
    if(!slot){blocked[target]=1;return;}
    std::optional<std::uint32_t> source;
    for(const auto& read:ssa.Reads(node)) {
      arena.Step();if(read.value.value>=nv)fail();
      if(SameSlot(ssa.Values()[read.value.value].slot,*slot)) {
        if(source && *source!=read.value.value)fail();source=read.value.value;
      }
    }
    add(target,source);
  };
  // Site writes use Before's SSA inputs, never post-Commit state.
  std::pmr::vector<std::optional<ExceptionNodeId>> before(flow.instructions.size(),{},memory);
  for(std::uint32_t n=0;n<nn;++n) {
    arena.Step();const auto& node=flow.nodes[n];if(node.instruction.value>=flow.instructions.size())fail();
    if(node.phase==ExceptionPhase::kBefore)before[node.instruction.value]=ExceptionNodeId{n};
  }
  for(std::uint32_t v=0;v<nv;++v) {
    arena.Step();const auto& origin=ssa.Values()[v].origin;
    if(const auto* site=std::get_if<InstructionValue>(&origin)) {
      if(site->instruction.value>=nn)fail();const auto& node=flow.nodes[site->instruction.value];
      if(node.phase==ExceptionPhase::kCatch)add(v,static_cast<std::uint32_t>(nv)+site->instruction.value);
      else if(node.phase==ExceptionPhase::kCommit && before[node.instruction.value]) {
        const auto& op=flow.instructions[node.instruction.value].operation;
        if(const auto* assignment=std::get_if<ScalarAssignment>(&op)) {
          if(const auto* value=std::get_if<ScalarValue>(&assignment->expression))scalar(v,*value,*before[node.instruction.value]);
          else blocked[v]=1;
        } else if(!std::holds_alternative<StringLoad>(op))blocked[v]=1;
      } else blocked[v]=1;
    } else if(std::holds_alternative<MethodEntry>(origin))blocked[v]=1;
    // Phi inputs are indexed once below, including unknown entry inputs.
  }
  for(const auto& input:ssa.AllInputs()) {
    arena.Step();if(input.phi.value>=nv || input.value.value>=nv)fail();
    add(input.phi.value,input.value.value);
  }
  std::pmr::vector<std::uint32_t> sources(nn,0,memory);
  for(std::uint32_t n=0;n<nn;++n) {
    arena.Step();const auto& node=flow.nodes[n];
    for(std::uint8_t e=0;e<node.successor_count;++e) {
      arena.Step();const auto arc=node.successors[e];if(arc.target.value>=nn)fail();
      if(flow.nodes[arc.target.value].phase!=ExceptionPhase::kCatch)continue;
      const auto c=arc.target.value,target=static_cast<std::uint32_t>(nv)+c;++sources[c];
      if(arc.kind!=ExceptionArcKind::kException || node.phase!=ExceptionPhase::kBefore){blocked[target]=1;continue;}
      const auto& op=flow.instructions[node.instruction.value].operation;
      if(const auto* thrown=std::get_if<ThrowOperation>(&op);thrown&&thrown->kind==ThrowKind::kValue)scalar(target,thrown->value,{n});
      else if(!RuntimeError(op) && !(policy==ExternalExceptionPolicy::kNoHoleV1 && External(flow.instructions[node.instruction.value])))blocked[target]=1;
    }
  }
  for(std::uint32_t n=0;n<nn;++n) {
    arena.Step();if(flow.nodes[n].phase!=ExceptionPhase::kCatch || !sources[n])blocked[nv+n]=1;
  }
  // Vacuous empty phis are unknown. Only supported instruction constants and
  // fully sourced Catch equations may seed the fixed point.
  for(std::uint32_t v=0;v<nv;++v)if(std::holds_alternative<PhiValue>(ssa.Values()[v].origin) && !needed[v])blocked[v]=1;
  std::sort(dependencies.begin(),dependencies.end(),[&](auto a,auto b){arena.Step();return a<b;});
  for(const auto& d:dependencies){arena.Step();++offsets[d.source+1];}
  for(std::size_t i=1;i<offsets.size();++i){arena.Step();offsets[i]+=offsets[i-1];}
  for(std::uint32_t n=0;n<count;++n)if(!blocked[n] && !needed[n]){known[n]=1;queue.push_back(n);}
  for(std::size_t i=0;i<queue.size();++i)for(auto d=offsets[queue[i]];d<offsets[queue[i]+1];++d) {
    arena.Step();const auto target=dependencies[d].target;
    if(--needed[target]==0 && !blocked[target]){known[target]=1;queue.push_back(target);}
  }
  std::pmr::vector<CatchPayloadFact> out(memory);
  for(std::uint32_t n=0;n<nn;++n)if(flow.nodes[n].phase==ExceptionPhase::kCatch) {
    arena.Step();out.push_back({{n},known[nv+n]?CatchPayloadDomain::kNonHole:CatchPayloadDomain::kHolePossible,sources[n]});
  }
  return out;
}
void VerifyCatchPayloadDomains(const ExceptionFlow& flow,const ExceptionSsa& ssa,
    ExternalExceptionPolicy policy,std::span<const CatchPayloadFact> supplied,RawEventArena& arena) {
  const auto expected=DeriveCatchPayloadDomains(flow,ssa,policy,arena);
  if(expected.size()!=supplied.size())throw RawEventFailure{RawEventError::kCorrespondence};
  for(std::size_t i=0;i<expected.size();++i) {
    arena.Step();if(expected[i]!=supplied[i])throw RawEventFailure{RawEventError::kCorrespondence};
  }
}
}  // namespace abcd::analysis
