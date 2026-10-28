#include "completion_ports.h"

#include <algorithm>
#include <bit>
#include <new>
#include <map>
#include <functional>
#include <stdexcept>
#include <tuple>

#include "exception_topology.h"

namespace abcd::source {
namespace {
using namespace analysis;
using Error = CompletionPortError;
void Need(bool valid, Error error) {
  if (!valid) throw CompletionPortFailure{error};
}
class Quota {
 public:
  explicit Quota(SemanticBudget b) : initial_(b), left_(b) {}
  void Step(std::uint64_t n = 1) {
    Need(n <= left_.work, Error::kWorkLimit); left_.work -= n;
  }
  void Admit(std::size_t n, std::size_t width = 1) {
    Need(n <= left_.bytes / width, Error::kResourceLimit); left_.bytes -= n * width;
  }
  template<class T> void Reserve(std::vector<T>& v, std::size_t n) {
    Admit(n, sizeof(T)); v.reserve(n); Admit(v.capacity() - n, sizeof(T));
  }
  template<class T> void Size(std::vector<T>& v, std::size_t n) {
    Step(n); Reserve(v, n); v.resize(n);
  }
  template<class T> void Push(std::vector<T>& v,T item) {
    if(v.size()==v.capacity())Reserve(v,v.empty()?1:v.size()*2);
    v.push_back(std::move(item));
  }
  SemanticBudget Remaining() const { return left_; }
  std::uint64_t Work() const { return initial_.work - left_.work; }
 private:
  SemanticBudget initial_, left_;
};
template<class T> struct PortAllocator {
  using value_type=T;
  Quota* quota;
  explicit PortAllocator(Quota& q):quota(&q){}
  template<class U>PortAllocator(const PortAllocator<U>& other):quota(other.quota){}
  T* allocate(std::size_t n){quota->Admit(n,sizeof(T));return std::allocator<T>{}.allocate(n);}
  void deallocate(T* p,std::size_t n){std::allocator<T>{}.deallocate(p,n);}
  template<class U>bool operator==(const PortAllocator<U>&)const{return true;}
};
struct PortPairLess {
  Quota* quota;
  bool operator()(const std::pair<std::uint32_t,std::uint32_t>& a,
      const std::pair<std::uint32_t,std::uint32_t>& b)const{quota->Step();return a<b;}
};
void Qualify(CompletionPorts& out,std::size_t protocols,Quota& q) {
  using Pair=std::pair<std::uint32_t,std::uint32_t>;
  using Item=std::pair<const Pair,std::uint32_t>;
  std::map<Pair,std::uint32_t,PortPairLess,PortAllocator<Item>> additions{PortPairLess{&q},PortAllocator<Item>{q}};
  std::map<Pair,std::uint32_t,PortPairLess,PortAllocator<Item>> states{PortPairLess{&q},PortAllocator<Item>{q}};
  q.Push(out.closed,CompletionClosedNode{});q.Push(out.closed,CompletionClosedNode{});
  const auto depth=std::bit_width(protocols);
  const auto contains=[&](std::uint32_t root,std::uint32_t protocol) {
    for(auto d=depth;d && root;--d){q.Step();Need(root<out.closed.size(),Error::kActivation);const auto& node=out.closed[root];root=(protocol>>(d-1))&1?node.right:node.left;}
    return root!=0;
  };
  const auto add_node=[&](auto&& self,std::uint32_t root,std::uint32_t protocol,unsigned d)->std::uint32_t {
    q.Step();if(!d)return 1;
    Need(root<out.closed.size(),Error::kActivation);auto node=out.closed[root];
    if((protocol>>(d-1))&1)node.right=self(self,node.right,protocol,d-1);
    else node.left=self(self,node.left,protocol,d-1);
    Need(out.closed.size()<UINT32_MAX,Error::kResourceLimit);
    const auto result=static_cast<std::uint32_t>(out.closed.size());q.Push(out.closed,node);return result;
  };
  const auto add=[&](std::uint32_t root,std::uint32_t protocol) {
    const Pair key{root,protocol};if(const auto found=additions.find(key);found!=additions.end())return found->second;
    const auto result=add_node(add_node,root,protocol,static_cast<unsigned>(depth));additions.emplace(key,result);return result;
  };
  const auto state=[&](std::uint32_t entry,std::uint32_t closed) {
    const Pair key{entry,closed};if(const auto found=states.find(key);found!=states.end())return found->second;
    Need(entry<out.entries.size() && out.qualified.size()<UINT32_MAX,Error::kResourceLimit);
    const auto id=static_cast<std::uint32_t>(out.qualified.size());
    const auto protocol=out.entries[entry].protocol.value;Need(protocol<protocols,Error::kInput);
    q.Push(out.qualified,CompletionQualifiedPort{entry,closed,!contains(closed,protocol),0,0});states.emplace(key,id);return id;
  };
  q.Size(out.roots,out.entries.size());
  for(std::uint32_t e=0;e<out.entries.size();++e) {
    q.Step();if(out.entries[e].disposition==CompletionPortDisposition::kAction)out.roots[e]=state(e,0);
  }
  for(std::size_t at=0;at<out.qualified.size();++at) {
    q.Step();const auto route=out.qualified[at];const auto& entry=out.entries[route.entry];
    const auto closed=route.executes?add(route.closed,entry.protocol.value):route.closed;
    Need(route.executes || entry.original.pending.kind==PendingCompletionKind::kThrow,Error::kActivation);
    out.qualified[at].first=static_cast<std::uint32_t>(out.transitions.size());
    for(auto p=out.first_exit[route.entry];p<out.first_exit[route.entry+1];++p) {
      q.Step();const auto& exit=out.exits[p];const auto& outcome=exit.original.outcome;
      const bool observed=outcome.kind!=CloseOutcomeKind::kUnproved;
      if(!observed)Need(outcome.gap==CloseOutcomeGap::kNotObserved,Error::kActivation);
      if(observed!=route.executes)continue;
      if(!observed)Need(outcome.gap==CloseOutcomeGap::kNotObserved &&
          exit.original.pending==entry.original.pending,Error::kActivation);
      std::optional<std::uint32_t> next;
      if(exit.next_entry)next=state(*exit.next_entry,closed);
      Need(out.transitions.size()<UINT32_MAX,Error::kResourceLimit);
      q.Push(out.transitions,CompletionQualifiedTransition{p,next});++out.qualified[at].count;
    }
    Need(out.qualified[at].count!=0,Error::kCoverage);
  }
}
template<class T>std::optional<Error> LimitCause(const T& failure) {
  if constexpr(std::is_enum_v<T>) {
    if constexpr(requires{T::kResourceLimit;})if(failure==T::kResourceLimit)return Error::kResourceLimit;
    if constexpr(requires{T::kWorkLimit;})if(failure==T::kWorkLimit)return Error::kWorkLimit;
  } else if constexpr(requires{std::variant_size<T>::value;}) {
    return std::visit([](const auto& value){return LimitCause(value);},failure);
  } else {
    if constexpr(requires{failure.kind;})if(const auto limit=LimitCause(failure.kind))return limit;
    if constexpr(requires{failure.cause;})if(const auto limit=LimitCause(failure.cause))return limit;
    if constexpr(requires{failure.single_pass;})if(failure.single_pass)if(const auto limit=LimitCause(*failure.single_pass))return limit;
  }
  return {};
}
Error Cause(CloseCompletionError e, Error otherwise) {
  return e == CloseCompletionError::kResourceLimit ? Error::kResourceLimit :
      e == CloseCompletionError::kWorkLimit ? Error::kWorkLimit : otherwise;
}
ProtectionContextId Context(const std::optional<CatchScope>& scope) {
  return {scope ? 1 + 2 * scope->region.value +
      (scope->part == CatchPart::kHandler ? 1u : 0u) : 0};
}
template<class T> std::size_t Bytes(const std::vector<T>& v) {
  return v.capacity() * sizeof(T);
}
std::size_t Retained(const CloseCompletionSummary& s) {
  auto bytes = Bytes(s.entries) + Bytes(s.regions);
  for (const auto& r : s.regions) {
    bytes += Bytes(r.members) + Bytes(r.edges);
    if (r.outcomes) {
      const auto& o = *r.outcomes;
      bytes += Bytes(o.arrivals) + Bytes(o.leaving) + Bytes(o.comparisons) + Bytes(o.catch_payloads) + Bytes(o.events);
    }
    if (r.boundary) {
      const auto& b = *r.boundary;
      bytes += Bytes(b.ports) + Bytes(b.unreachable_entries) + Bytes(b.private_slots) +
          Bytes(b.regions) + Bytes(b.enclosures) + Bytes(b.protected_ranges);
      for (const auto& p : b.protected_ranges)
        bytes += Bytes(p.ordinary) + Bytes(p.transport) + Bytes(p.protocol) + Bytes(p.dispatch) + Bytes(p.exits);
    }
  }
  return bytes;
}
std::size_t Retained(const CloseEntries& s) {
  auto bytes = Bytes(s.closes);
  for (const auto& c : s.closes)
    bytes += Bytes(c.entries) + Bytes(c.exits) + Bytes(c.relation.arrivals) + Bytes(c.relation.leaving);
  return bytes;
}
CompletionPorts Build(const abc::MethodBody& body, abc::ProfileId profile,
    const ValueProgram& graph, const PhasedValueInput& input,
    const ExceptionPlan& plan, const IteratorKeys& keys,
    const IteratorProtocolScan& scan, const ExceptionControlRegionFailure& refused,
    SemanticBudget budget, SourceContract contract) {
  Quota q(budget);
  Need(refused.kind == ExceptionControlError::kMultipleExits &&
      refused.region.value < plan.regions.size(), Error::kInput);
  // A typed failure is an activation boundary, never a proof assumption.
  // Both witness edges must still exist in this exact immutable input.
  for (const auto& exit : refused.exits) {
    q.Step(); const auto* e = std::get_if<EdgeId>(&exit.source);
    Need(e && e->value < graph.edges.size(), Error::kEntry);
    const auto& raw = graph.edges[e->value];
    Need(exit.from == raw.source && exit.target == raw.target, Error::kEntry);
    Need(raw.source.value < graph.phases.size() && raw.target.value < graph.phases.size() &&
        raw.kind != FlowEdgeKind::kException, Error::kEntry);
  }
  Need(refused.exits[0].continuation != refused.exits[1].continuation, Error::kEntry);
  // Retain the output indexes while delegated replays are live. Admission
  // happens before either allocation or the legacy control query.
  CompletionPorts out{keys.method, graph.revision, graph.cfg_revision,
      refused.region, refused.exits};
  q.Size(out.wrappers, plan.regions.size());
  q.Size(out.protection, 1 + 2 * plan.regions.size());
  q.Size(out.contexts, input.flow.nodes.size());
  // Exit witnesses may name an effect-free jump AFTER the protected range.
  // Replay the ordinary owner instead of inventing an interval admission rule.
  // A failed control query exposes no telemetry: prepay its bounded grant.
  constexpr std::uint64_t kControlWorkPerInput = 4096;
  const auto count=std::uint64_t(graph.blocks.size())+graph.edges.size()+plan.regions.size()+1;
  Need(count<=UINT32_MAX,Error::kResourceLimit);
  const auto grant=std::min(q.Remaining().work,kControlWorkPerInput*count*(1+std::bit_width(count)));
  auto legacy=BuildExceptionControl(body,graph,input,plan,{q.Remaining().bytes,grant});
  q.Step(grant);
  if(const auto limit=LimitCause(legacy))throw CompletionPortFailure{*limit};
  const auto* original=std::get_if<ExceptionControlRegionFailure>(&legacy);
  Need(original && original->kind==ExceptionControlError::kMultipleExits &&
      original->region==refused.region && original->exits==refused.exits,Error::kEntry);
  bool covered = false;
  for (const auto& r : scan.regions) {
    q.Step(); covered |= r.region == refused.region &&
        r.role == IteratorRegionRole::kReturnProtection;
  }
  Need(covered, Error::kCoverage);
  auto analyzed = AnalyzeCloseCompletion(body, profile, graph, input, plan,
      keys, scan, q.Remaining(), contract,{},CompletionReplay::kAllProtocols);
  if (const auto* f = std::get_if<CloseCompletionFailure>(&analyzed))
    throw CompletionPortFailure{Cause(f->kind, Error::kPriority),{},{},*f};
  const auto& summary = std::get<CloseCompletionSummary>(analyzed);
  q.Step(summary.work);
  // Diagnostic buffers remain alive during the reverse verifier. The peak
  // includes delegated temporary grants, which have already been released.
  q.Admit(Retained(summary));
  const auto checked = VerifyCloseCompletion(body, profile, graph, input, plan,
      keys, scan, summary, q.Remaining(), contract);
  if (const auto* f = std::get_if<CloseCompletionFailure>(&checked))
    throw CompletionPortFailure{Cause(f->kind, Error::kCorrespondence)};
  q.Step(std::get<std::uint64_t>(checked));
  for (const auto& e : summary.entries) {
    q.Step();
    for (const auto obligation : {CloseObligation::kGuard,
        CloseObligation::kReference, CloseObligation::kPriority}) {
      const auto& fact = e.obligations[static_cast<std::size_t>(obligation)];
      Need(fact.state == CloseProofState::kVerified && !fact.missing, Error::kPriority);
    }
  }
  auto derived = DeriveCloseEntries(body, profile, graph, input, plan, keys, scan, q.Remaining(),CompletionReplay::kAllProtocols);
  if (const auto* f = std::get_if<CloseEntryFailure>(&derived))
    throw CompletionPortFailure{f->kind == CloseEntryError::kResourceLimit ? Error::kResourceLimit :
        f->kind == CloseEntryError::kWorkLimit ? Error::kWorkLimit : Error::kEntry};
  const auto& closes = std::get<CloseEntries>(derived);
  q.Step(closes.work); q.Admit(Retained(closes));
  out.activation=original->exception_region;
  for (std::uint32_t r = 0; r < plan.regions.size(); ++r) {
    q.Step(); const auto parent = Context(plan.layout.regions[r].parent);
    for (const auto part : {CatchPart::kProtected, CatchPart::kHandler}) {
      const CatchScope scope{{r}, part};
      out.protection[Context(scope).value] = {parent, scope};
    }
  }
  for (const auto& r : scan.regions) if (r.role == IteratorRegionRole::kEnclosingHandler) {
    q.Step(); auto& owner = out.wrappers[r.region.value];
    Need(!owner || *owner == r.protocol, Error::kProtection); owner = r.protocol;
  }
  ControlQuota projection(q.Remaining());
  auto scopes = ProjectExceptionScopes(graph, plan, {}, projection);
  q.Step(q.Remaining().work - projection.RemainingWork());
  q.Admit(q.Remaining().bytes - projection.Remaining().bytes);
  for (std::uint32_t b = 0; b < graph.phases.size(); ++b) {
    q.Step(); out.contexts[graph.phases[b].input.value] = Context(scopes[b]);
  }
  std::size_t entry_count = 0, exit_count = 0, member_count = 0;
  for (const auto& c : closes.closes) { q.Step(); entry_count += c.entries.size(); }
  for (const auto& r : summary.regions) {
    q.Step(); Need(r.outcomes.has_value(), Error::kPriority);
    exit_count += r.outcomes->leaving.size();
    member_count += r.members.size();
  }
  q.Reserve(out.entries, entry_count); q.Reserve(out.exits, exit_count);
  q.Reserve(out.members, member_count);
  for (const auto& r : summary.regions) {
    Need(r.boundary && r.boundary->entry_coverage==CloseProofState::kVerified &&
        r.boundary->port_coverage==CloseProofState::kVerified &&
        r.boundary->private_state==CloseProofState::kVerified,Error::kCoverage);
    for (const auto& m : r.members) {
    q.Step(); Need(m.role != CloseMemberRole::kUnproved, Error::kCoverage);
    out.members.push_back({m.node, r.protocol, r.read,m.role});
  }}
  std::sort(out.members.begin(), out.members.end(), [&](const auto& a, const auto& b) { q.Step(); return a < b; });
  out.members.erase(std::unique(out.members.begin(), out.members.end()), out.members.end());
  // One immutable instruction-range index serves every body query. Do not
  // scan the complete port inventory once per protected range.
  q.Reserve(out.range_members,out.members.size());
  for(const auto& member:out.members) {
    q.Step();out.range_members.push_back({input.flow.nodes[member.node.value].instruction,member.node});
  }
  std::sort(out.range_members.begin(),out.range_members.end(),[&](const auto& a,const auto& b){q.Step();return a<b;});
  out.range_members.erase(std::unique(out.range_members.begin(),out.range_members.end()),out.range_members.end());
  for (const auto& c : closes.closes)
    for (std::uint32_t i = 0; i < c.entries.size(); ++i) {
      q.Step(); const auto& e = c.entries[i];
      out.entries.push_back({c.protocol, c.read, i, e.entry, e.origin, e.catcher});
    }
  std::sort(out.entries.begin(), out.entries.end(), [&](const auto& a, const auto& b) {
    q.Step(); return a.original.edge < b.original.edge;
  });
  for (std::size_t i = 1; i < out.entries.size(); ++i) {
    q.Step(); Need(out.entries[i-1].original.edge != out.entries[i].original.edge, Error::kEntry);
  }
  for (const auto& r : summary.regions) for (const auto& p : r.outcomes->leaving) {
    q.Step(); const auto source = std::visit([](const auto& x) { return x.source; }, p.exit);
    CompletionPortExit port{r.protocol, r.read, p, out.contexts[source.value], {0}};
    if (const auto* edge = std::get_if<CompletionEdge>(&p.exit)) {
      const auto& n = input.flow.nodes[edge->source.value];
      Need(edge->successor < n.successor_count, Error::kEntry);
      port.target = out.contexts[n.successors[edge->successor].target.value];
      const auto next = std::lower_bound(out.entries.begin(), out.entries.end(), *edge,
          [&](const auto& a, auto b) { q.Step(); return a.original.edge < b; });
      if (next != out.entries.end() && next->original.edge == *edge) {
        Need(next->protocol != r.protocol || next->read != r.read, Error::kOrder);
        if(next->original.pending.kind!=p.pending.kind)throw CompletionPortFailure{Error::kPayload,*edge};
        // Return values retain their exact SSA identity. For a Throw, both
        // independently derived tables refer to the SAME physical exception
        // edge; Catch defines its transported payload only after that edge.
        if (p.pending.kind != PendingCompletionKind::kThrow && next->original.pending.payload != p.pending.payload) {
          throw CompletionPortFailure{Error::kPayload,*edge};
        }
        const auto& op = input.flow.instructions[n.instruction.value].operation;
        if (std::holds_alternative<ThrowOperation>(op)) {
          const auto reads = input.ssa.Reads(edge->source);
          Need(reads.size() == 1, Error::kPayload); port.binding = reads[0].value;
        }
        port.next_entry = static_cast<std::uint32_t>(next - out.entries.begin());
      }
    }
    out.exits.push_back(port);
  }
  std::vector<std::uint32_t> incoming;
  q.Size(incoming,out.entries.size());
  for(const auto& p:out.exits)if(p.next_entry){q.Step();++incoming[*p.next_entry];}
  for(std::size_t e=0;e<out.entries.size();++e) {
    q.Step(1+std::bit_width(out.members.size()));
    const auto source=out.entries[e].original.edge.source;
    const auto found=std::lower_bound(out.members.begin(),out.members.end(),CompletionPortMember{source,{},{}},
        [](const auto& a,const auto& b){return a<b;});
    if(found!=out.members.end() && found->node==source) {
      auto& entry=out.entries[e];
      if(incoming[e]){entry.disposition=CompletionPortDisposition::kContinuation;continue;}
      const auto& node=input.flow.nodes[source.value];
      bool comparison=false,infeasible=false;
      for(auto it=found;it!=out.members.end() && it->node==source;++it) {
        q.Step();comparison|=it->role==CloseMemberRole::kComparison;
        infeasible|=it->role==CloseMemberRole::kInfeasible;
      }
      if(comparison && node.phase==ExceptionPhase::kBefore &&
          node.successors[entry.original.edge.successor].kind==ExceptionArcKind::kException)
        entry.disposition=CompletionPortDisposition::kExcludedComparison;
      else if(infeasible)entry.disposition=CompletionPortDisposition::kInfeasible;
      else throw CompletionPortFailure{Error::kCoverage,entry.original.edge};
    }
  }
  std::vector<std::tuple<OpId,std::uint32_t,std::uint32_t>> ordinals;
  q.Reserve(ordinals,out.entries.size());
  for(std::uint32_t e=0;e<out.entries.size();++e){q.Step();ordinals.emplace_back(out.entries[e].read,out.entries[e].entry,e);}
  std::sort(ordinals.begin(),ordinals.end(),[&](const auto& a,const auto& b){q.Step();return a<b;});
  q.Size(out.first_exit,out.entries.size()+1);
  for(auto& p:out.exits) {
    q.Step(1+std::bit_width(ordinals.size()));
    const auto it=std::lower_bound(ordinals.begin(),ordinals.end(),std::tuple{p.read,p.original.entry,0u});
    Need(it!=ordinals.end() && std::get<0>(*it)==p.read && std::get<1>(*it)==p.original.entry,Error::kEntry);
    p.entry=std::get<2>(*it);++out.first_exit[p.entry+1];
  }
  std::sort(out.exits.begin(),out.exits.end(),[&](const auto& a,const auto& b){q.Step();return std::tie(a.entry,a.original)<std::tie(b.entry,b.original);});
  for(std::size_t e=1;e<out.first_exit.size();++e){q.Step();out.first_exit[e]+=out.first_exit[e-1];}
  q.Reserve(out.order,out.entries.size());
  for(std::uint32_t e=0;e<incoming.size();++e)if(!incoming[e])out.order.push_back(e);
  for(std::size_t i=0;i<out.order.size();++i) {
    q.Step();const auto e=out.order[i];
    for(auto p=out.first_exit[e];p<out.first_exit[e+1];++p)if(const auto next=out.exits[p].next_entry) {
      q.Step();Need(incoming[*next]>0,Error::kOrder);
      if(!--incoming[*next])out.order.push_back(*next);
    }
  }
  Need(out.order.size()==out.entries.size(),Error::kOrder);
  q.Reserve(out.gates,closes.closes.size());
  for(const auto& close:closes.closes) {
    q.Step();const auto definition=input.ssa.Write(close.state_write);Need(definition.has_value(),Error::kActivation);
    const auto* slot=std::get_if<abc::RegisterId>(&input.ssa.Values()[definition->value].slot);Need(slot,Error::kActivation);
    out.gates.push_back({close.protocol,close.read,close.predicate,close.state_write,*definition,*slot});
  }
  Qualify(out,scan.candidates.size(),q);
  // One sparse def-use closure for all payloads. A phi input keeps its real
  // predecessor; no canonical value is equated across dynamic loop executions.
  std::vector<std::uint8_t> seen;
  std::vector<SsaValueId> pending;
  std::vector<ExceptionNodeId> before;
  q.Size(seen,input.ssa.Values().size()); q.Reserve(pending,seen.size());
  q.Size(before,input.flow.instructions.size());
  for(std::uint32_t n=0;n<input.flow.nodes.size();++n) {
    q.Step();const auto& node=input.flow.nodes[n];
    if(node.phase==ExceptionPhase::kBefore)before[node.instruction.value]={n};
  }
  const auto add=[&](SsaValueId value) {
    q.Step();Need(value.value<seen.size(),Error::kPayload);
    if(!seen[value.value]){seen[value.value]=1;pending.push_back(value);}
  };
  const auto seed=[&](const CompletionPayload& payload) {
    if(const auto* value=std::get_if<SsaValueId>(&payload))add(*value);
  };
  for(const auto& e:out.entries){seed(e.original.pending.payload);if(e.original.binding)add(*e.original.binding);}
  for(const auto& e:out.exits){seed(e.original.pending.payload);if(e.binding)add(*e.binding);}
  q.Reserve(out.definitions,seen.size());
  q.Reserve(out.payloads,input.ssa.AllInputs().size()+input.ssa.AllReads().size());
  for(std::size_t i=0;i<pending.size();++i) {
    q.Step();const auto id=pending[i];const auto& value=input.ssa.Values()[id.value];
    CompletionPayloadDefinition definition{id,CompletionDefinitionKind::kEntry,{}};
    if(std::holds_alternative<PhiValue>(value.origin)) {
      definition.kind=CompletionDefinitionKind::kPhi;
      for(const auto& arm:input.ssa.Inputs(id)) {
        q.Step();const auto* edge=std::get_if<SsaEdge>(&arm.edge);
        Need(edge,Error::kPayload);
        out.payloads.push_back(CompletionPayloadPhi{id,arm.value,{{edge->source.value},edge->successor}});
        add(arm.value);
      }
    } else if(const auto* origin=std::get_if<InstructionValue>(&value.origin)) {
      const ExceptionNodeId point{origin->instruction.value};
      Need(point.value<input.flow.nodes.size(),Error::kPayload);
      const auto& node=input.flow.nodes[point.value];definition.point=point;
      definition.kind=node.phase==ExceptionPhase::kCatch?CompletionDefinitionKind::kCatch:CompletionDefinitionKind::kCommit;
      Need(node.phase==ExceptionPhase::kCatch || node.phase==ExceptionPhase::kCommit,Error::kPayload);
      if(const auto* assignment=std::get_if<ScalarAssignment>(&input.flow.instructions[node.instruction.value].operation);
          assignment && std::holds_alternative<ScalarValue>(assignment->expression)) {
        const auto reads=input.ssa.Reads(before[node.instruction.value]);
        Need(reads.size()<=1,Error::kPayload);
        if(!reads.empty()) {
          out.payloads.push_back(CompletionPayloadCopy{id,reads[0].value,point});add(reads[0].value);
        }
      }
    }
    out.definitions.push_back(definition);
  }
  // Keep every original normal exit of return protection separately. In
  // particular call-success/handler -> selection is NOT missing -> bypass.
  std::vector<std::optional<TryId>> protected_by;
  q.Size(protected_by, input.flow.instructions.size());
  for (const auto& r : scan.regions) if (r.role == IteratorRegionRole::kReturnProtection) {
    const auto& range = plan.layout.regions[r.region.value].body;
    for (auto i = range.begin.value; i < range.end.value; ++i) {
      q.Step(); Need(!protected_by[i] || *protected_by[i] == r.region, Error::kProtection);
      protected_by[i] = r.region;
    }
  }
  q.Reserve(out.resumes, graph.edges.size());
  for (std::uint32_t n = 0; n < input.flow.nodes.size(); ++n) {
    q.Step(); const auto& node = input.flow.nodes[n];
    const auto region = protected_by[node.instruction.value]; if (!region) continue;
    for (std::uint8_t e = 0; e < node.successor_count; ++e) {
      q.Step(); const auto& a = node.successors[e];
      if (a.kind == ExceptionArcKind::kException ||
          protected_by[input.flow.nodes[a.target.value].instruction.value] == region) continue;
      Need(out.resumes.size()<out.resumes.capacity(),Error::kResourceLimit);
      out.resumes.push_back({*region, {{n}, e}, a.target, out.contexts[n], out.contexts[a.target.value]});
    }
  }
  const auto bytes = [](const auto& v) { return v.capacity() * sizeof(typename std::decay_t<decltype(v)>::value_type); };
  out.allocated_bytes = bytes(out.protection) + bytes(out.contexts) + bytes(out.entries) +
      bytes(out.range_members) + bytes(out.exits) + bytes(out.resumes) + bytes(out.wrappers) + bytes(out.members) + bytes(out.definitions) + bytes(out.payloads) + bytes(out.first_exit) + bytes(out.order) + bytes(out.gates) + bytes(out.closed) + bytes(out.qualified) + bytes(out.transitions) + bytes(out.roots);
  out.work = q.Work(); return out;
}
}  // namespace
std::variant<CompletionPorts, CompletionPortFailure> DeriveCompletionPorts(
    const abc::MethodBody& b, abc::ProfileId p, const ValueProgram& g,
    const PhasedValueInput& in, const ExceptionPlan& r, const IteratorKeys& k,
    const IteratorProtocolScan& s, const ExceptionControlRegionFailure& f,
    SemanticBudget budget, SourceContract contract) {
  try { return Build(b,p,g,in,r,k,s,f,budget,contract); }
  catch (const CompletionPortFailure& error) { return error; }
  catch (const ControlError error) { return CompletionPortFailure{error == ControlError::kWorkLimit ? Error::kWorkLimit : Error::kResourceLimit}; }
  catch (const ExceptionControlError) { return CompletionPortFailure{Error::kProtection}; }
  catch (const std::bad_alloc&) { return CompletionPortFailure{Error::kResourceLimit}; }
  catch (const std::length_error&) { return CompletionPortFailure{Error::kResourceLimit}; }
}
std::variant<std::uint64_t, CompletionPortFailure> VerifyCompletionPorts(
    const abc::MethodBody& b, abc::ProfileId p, const ValueProgram& g,
    const PhasedValueInput& in, const ExceptionPlan& r, const IteratorKeys& k,
    const IteratorProtocolScan& s, const CompletionPorts& supplied,
    SemanticBudget budget, SourceContract contract) {
  const ExceptionControlRegionFailure original{ExceptionControlError::kMultipleExits,
      supplied.refused_region, supplied.refused_exits,supplied.activation};
  auto built = DeriveCompletionPorts(b,p,g,in,r,k,s,original,budget,contract);
  if (const auto* error = std::get_if<CompletionPortFailure>(&built)) return *error;
  const auto& expected = std::get<CompletionPorts>(built);
  const auto fail = [](Error e) { return CompletionPortFailure{e}; };
  const auto cost = supplied.protection.size() + supplied.contexts.size() + supplied.wrappers.size() +
      supplied.range_members.size()+supplied.entries.size() + supplied.exits.size() + supplied.resumes.size() + supplied.members.size() + supplied.definitions.size() + supplied.payloads.size()+supplied.first_exit.size()+supplied.order.size()+supplied.gates.size()+supplied.closed.size()+supplied.qualified.size()+supplied.transitions.size()+supplied.roots.size();
  if (cost > budget.work - expected.work) return fail(Error::kWorkLimit);
  if (supplied.method != expected.method || supplied.revision != expected.revision ||
      supplied.cfg_revision != expected.cfg_revision || supplied.activation!=expected.activation) return fail(Error::kInput);
  if (supplied.protection != expected.protection || supplied.contexts != expected.contexts ||
      supplied.wrappers != expected.wrappers) return fail(Error::kProtection);
  if (supplied.entries != expected.entries) return fail(Error::kEntry);
  if (supplied.exits != expected.exits) return fail(Error::kPayload);
  if (supplied.definitions != expected.definitions || supplied.payloads != expected.payloads) return fail(Error::kPayload);
  if(supplied.gates!=expected.gates)return fail(Error::kActivation);
  if(supplied.first_exit!=expected.first_exit || supplied.order!=expected.order || supplied.closed!=expected.closed || supplied.qualified!=expected.qualified || supplied.transitions!=expected.transitions || supplied.roots!=expected.roots)return fail(Error::kOrder);
  if (supplied.resumes != expected.resumes || supplied.members != expected.members || supplied.range_members!=expected.range_members) return fail(Error::kCoverage);
  return expected.work + cost;
}
std::optional<CompletionPortError> CompletionPortLimit(const CompletionPortFailure& failure) {
  return LimitCause(failure);
}
bool CompletionPortOwns(const CompletionPorts& ports,ExceptionNodeId node,IteratorProtocolId protocol) {
  const auto found=std::lower_bound(ports.members.begin(),ports.members.end(),
      CompletionPortMember{node,protocol,{},CloseMemberRole::kScalar},
      [](const auto& a,const auto& b){return a<b;});
  return found!=ports.members.end() && found->node==node && found->protocol==protocol;
}
}  // namespace abcd::source
