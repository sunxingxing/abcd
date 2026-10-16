#include "for_of_iteration.h"
#include "exception_topology.h"
#include "../analysis/idf_scratch.h"
#include "../analysis/value_phi.h"
#include <map>
#include <deque>
#include <algorithm>
#include <stdexcept>
#include <set>
#include <tuple>
namespace abcd::source {
namespace {
using namespace analysis;
struct Quota {
  analysis::detail::IdfScratch memory;
  std::uint64_t left,initial;
  ForOfIterationFailure witness{ForOfIterationError::kCorrespondence};
  explicit Quota(SemanticBudget b):memory(b.bytes),left(b.work),initial(b.work){}
  void Step(std::uint64_t n=1){if(n>left)Fail(ForOfIterationError::kWorkLimit);left-=n;}
  [[noreturn]] void Fail(ForOfIterationError e){witness.kind=e;throw witness;}
  void Require(bool yes,ForOfIterationError e=ForOfIterationError::kCorrespondence){if(!yes)Fail(e);}
  SemanticBudget Remaining()const{return {memory.Available(),left};}
  template<class T>void Reserve(std::vector<T>& v,std::size_t n){Require(n<=UINT32_MAX && n<=memory.Available()/sizeof(T),ForOfIterationError::kResourceLimit);Step(n);memory.Retain(n*sizeof(T));v.reserve(n);memory.Retain((v.capacity()-n)*sizeof(T));}
};
ForOfIterationError Resource(ForOfIdentityError e){return e==ForOfIdentityError::kAcquisitionSemantics?ForOfIterationError::kAcquisitionSemantics:e==ForOfIdentityError::kResourceLimit?ForOfIterationError::kResourceLimit:e==ForOfIdentityError::kWorkLimit?ForOfIterationError::kWorkLimit:ForOfIterationError::kIdentity;}
// Sparse (protocol,node) closure, finite phase domain {unadvanced,advanced,value-read}. A dense
// set of queries refuses before allocation; exhaustion is never a proof. Each
// state can change only three times, with bounded normal fanout. Ordered keys
// give O((V+E) log V) admitted work with a linear prepaid state allowance.
using Query=std::pair<std::uint32_t,std::uint32_t>;
struct Count {std::uint8_t bits=0;};
constexpr std::uint8_t kUnadvanced=1, kAdvanced=2, kValueRead=4;
// Resource admission for sparse query pairs, not a semantic selection limit.
constexpr std::size_t kSparseQueriesPerNode=8;
std::variant<ForOfIterationProof,ForOfIterationFailure> Analyze(
    const abc::MethodBody& body,const ValueProgram& g,const PhasedValueInput& input,
    const ExceptionPlan& regions,const ExceptionControl& control,const VerifiedProtocolRegionOwnership& owner,
    SourceContract contract,const ForOfIdentityIndex& identity,SemanticBudget budget,bool reverse) {
  try {
    Quota q(budget);
    // The delegated verifier exposes work, not scratch telemetry. Its entire
    // exclusive grant is a conservative peak; none is retained afterwards.
    q.memory.Observe(q.memory.Available());
    auto verified=VerifyForOfIdentityIndex(body,g,input,regions,control,owner,contract,identity,q.Remaining());
    if(const auto* e=std::get_if<ForOfIdentityFailure>(&verified))return ForOfIterationFailure{Resource(e->kind),{},e->operation};
    q.Step(std::get<std::uint64_t>(verified));
    ControlQuota topology_quota(q.Remaining());
    auto topology=ProjectProtocolControl(g,regions,input,owner,topology_quota);
    q.memory.Retain(q.Remaining().bytes-topology_quota.Remaining().bytes);
    q.Step(q.left-topology_quota.RemainingWork());
    std::pmr::vector<ControlNodeId> roots(&q.memory);
    roots.reserve(topology.entries.size());
    for(auto e:topology.entries){q.Step();roots.push_back({e.value});}
    auto discovered=BuildLoopForest(ControlGraphView{ControlProjection{g.revision,g.cfg_revision,topology.blocks,topology.edges}},roots,q.Remaining());
    if(const auto* e=std::get_if<LoopError>(&discovered))q.Fail(*e==LoopError::kResourceLimit?ForOfIterationError::kResourceLimit:*e==LoopError::kWorkLimit?ForOfIterationError::kWorkLimit:ForOfIterationError::kAdvance);
    auto loops=std::get<LoopForest>(std::move(discovered));q.memory.Retain(loops.allocated_bytes);q.Step(loops.work);
    std::pmr::vector<std::optional<ControlNodeId>> phase(g.blocks.size(),{},&q.memory),raw(input.flow.nodes.size(),{},&q.memory);
    for(std::uint32_t n=0;n<topology.mapping.units.size();++n){q.Step();if(const auto* p=std::get_if<PhaseControlUnit>(&topology.mapping.units[n])){q.Require(p->block.value<phase.size());phase[p->block.value]=ControlNodeId{n};raw[g.phases[p->block.value].input.value]=ControlNodeId{n};}}
    std::pmr::vector<std::optional<WorkBlockId>> commit(g.ops.size(),{},&q.memory);
    for(std::uint32_t b=0;b<g.phases.size();++b){q.Step();const auto& p=g.phases[b];if(p.phase==ExceptionPhase::kCommit && p.operation)commit[p.operation->value]=WorkBlockId{b};}
    ForOfIterationProof out{g.revision,g.cfg_revision};q.Reserve(out.iterations,identity.loops.size());
    std::pmr::map<Query,Count> counts(&q.memory);
    std::pmr::deque<Query> queue(&q.memory);
    // One method-wide sparse arena, not a separate V-sized buffer per loop.
    q.Require(topology.blocks.size()<=UINT32_MAX/kSparseQueriesPerNode,ForOfIterationError::kResourceLimit);
    const auto max_states=topology.blocks.size()*kSparseQueriesPerNode;
    const auto add=[&](Query query,std::uint8_t bits){
      q.Step();auto found=counts.find(query);
      if(found==counts.end()){q.Require(counts.size()<max_states,ForOfIterationError::kResourceLimit);found=counts.emplace(query,Count{}).first;}
      if((found->second.bits|bits)!=found->second.bits){found->second.bits|=bits;queue.push_back(query);}
    };
    for(const auto& l:identity.loops){
      q.Step();q.witness={ForOfIterationError::kCorrespondence,l.protocol};
      const auto at=[&](OpId o){q.witness.operation=o;q.Require(o.value<g.ops.size() && phase[g.ops[o.value].block.value],ForOfIterationError::kIdentity);return *phase[g.ops[o.value].block.value];};
      const auto a=at(l.advance),v=at(l.value);
      q.Require(commit[l.decision.value] && phase[commit[l.decision.value]->value],ForOfIterationError::kDone);
      const auto d=*phase[commit[l.decision.value]->value];
      q.Require(l.exhausted.source.value<raw.size() && raw[l.exhausted.source.value]==d && l.success.value<raw.size() && raw[l.success.value],ForOfIterationError::kDone);
      std::optional<ControlArcId> success,exhausted;
      const auto& b=topology.blocks[d.value];
      for(std::uint32_t e=b.first_arc.value;e<b.first_arc.value+b.arc_count;++e){q.Step();const auto original=topology.mapping.arcs[e].edge;q.Require(original.has_value());
        const auto& edge=g.edges[original->value];if(edge.successor==l.exhausted.successor)exhausted=ControlArcId{e};else if(topology.edges[e].target==*raw[l.success.value])success=ControlArcId{e};}
      q.Require(success && exhausted && success!=exhausted,ForOfIterationError::kDone);
      const auto loop=loops.owners[a.value];
      const auto header=loop?loops.loops[loop->value].header:a;
      q.witness={ForOfIterationError::kEntry,l.protocol,l.advance,header};
      if(l.header)q.Require(l.header->value<raw.size() && raw[l.header->value]==header,ForOfIterationError::kEntry);
      else q.Require(!loop,ForOfIterationError::kEntry);
      out.iterations.push_back({l.protocol,header,a,v,d,*success,*exhausted,loop});
      add({l.protocol.value,header.value},kUnadvanced);
    }
    std::pmr::vector<std::uint32_t> begin(loops.loops.size(),0,&q.memory),end(loops.loops.size(),0,&q.memory);
    std::pmr::vector<std::optional<LoopId>> first(loops.loops.size()+1,{},&q.memory),next(loops.loops.size(),{},&q.memory);
    struct Visit{LoopId loop;bool leave;};std::pmr::vector<Visit> stack(&q.memory);stack.reserve(loops.loops.size()*2);
    for(std::uint32_t l=0;l<loops.loops.size();++l){q.Step();const auto p=loops.loops[l].parent;const auto slot=p?p->value:loops.loops.size();next[l]=first[slot];first[slot]=LoopId{l};}
    for(auto l=first[loops.loops.size()];l;l=next[l->value]){q.Step();stack.push_back({*l,false});}
    std::uint32_t stamp=0;
    while(!stack.empty()){q.Step();const auto v=stack.back();stack.pop_back();if(v.leave){end[v.loop.value]=stamp;continue;}begin[v.loop.value]=stamp++;stack.push_back({v.loop,true});for(auto l=first[v.loop.value];l;l=next[l->value]){q.Step();stack.push_back({*l,false});}}
    const auto inside=[&](std::optional<LoopId> outer,ControlNodeId n){const auto inner=loops.owners[n.value];return outer && inner && begin[outer->value]<=begin[inner->value] && begin[inner->value]<end[outer->value];};
    for(const auto& p:out.iterations){q.Step();q.Require(!p.loop || !inside(p.loop,topology.edges[p.exhausted.value].target),ForOfIterationError::kDone);}
    while(!queue.empty()){
      q.Step();const auto key=reverse?queue.back():queue.front();if(reverse)queue.pop_back();else queue.pop_front();
      const auto& proof=out.iterations[key.first];const ControlNodeId n{key.second};q.witness={ForOfIterationError::kCorrespondence,proof.protocol,{},n};
      if(const auto* unit=std::get_if<PhaseControlUnit>(&topology.mapping.units[n.value]))q.witness.operation=g.phases[unit->block.value].operation;
      auto bits=counts.find(key)->second.bits;
      if(n==proof.advance){q.Require(!(bits&(kAdvanced|kValueRead)),ForOfIterationError::kAdvance);bits=kAdvanced;}
      if(n==proof.value){q.Require(bits==kAdvanced,ForOfIterationError::kAdvance);bits=kValueRead;}
      if(n==proof.decision)q.Require(bits==kAdvanced,ForOfIterationError::kAdvance);
      const auto& b=topology.blocks[n.value];
      // Every continuation must actually reach next. A normal abrupt exit,
      // or a possible exception before it, is not a completed iteration.
      if(bits&kUnadvanced){
        q.Require(b.arc_count!=0 && b.termination!=ControlTermination::kAbrupt,ForOfIterationError::kAdvance);
        if(const auto* unit=std::get_if<PhaseControlUnit>(&topology.mapping.units[n.value])){
          const auto& site=g.phases[unit->block.value];if(site.phase==ExceptionPhase::kBefore && site.operation)q.Require(g.ops[site.operation->value].exceptional==ExceptionalCompletion::kImpossible,ForOfIterationError::kAdvance);
        }
      }
      for(std::uint32_t e=b.first_arc.value;e<b.first_arc.value+b.arc_count;++e){q.Step();q.witness.edge=ControlArcId{e};const auto to=topology.edges[e].target;
        if(ControlArcId{e}==proof.exhausted)continue;
        // A pre-advance cycle would diverge without calling next; read-free
        // cycles are not evidence permitting removal of that control.
        if((bits&kUnadvanced) && loops.backedges[e])q.Fail(ForOfIterationError::kAdvance);
        if(proof.loop && loops.backedges[e]==proof.loop){q.Require(bits==kValueRead && to==proof.header,ForOfIterationError::kAdvance);continue;}
        if(proof.loop && !inside(proof.loop,to)){q.Require(!(bits&kUnadvanced),ForOfIterationError::kAdvance);continue;}
        add({key.first,to.value},bits);
      }
    }
    for(const auto& p:out.iterations){q.Step();const auto f=counts.find({p.protocol.value,p.value.value});q.Require(f!=counts.end() && f->second.bits==kAdvanced,ForOfIterationError::kAdvance);}
    // Activation-level cache proof. Reset only on the acquisition's NORMAL
    // Commit. A prior iteration's cached next never satisfies a fresh acquire.
    // External exceptional entries are roots too, not fabricated normal edges.
    {
      constexpr std::uint8_t kAbsent=1,kFresh=2,kCached=4;
      std::pmr::map<Query,Count> activation(&q.memory);std::pmr::deque<Query> pending(&q.memory);
      const auto offer=[&](Query key,std::uint8_t bits){q.Step();auto f=activation.find(key);if(f==activation.end()){q.Require(activation.size()<max_states,ForOfIterationError::kResourceLimit);f=activation.emplace(key,Count{}).first;}if((f->second.bits|bits)!=f->second.bits){f->second.bits|=bits;pending.push_back(key);}};
      for(const auto& l:identity.loops)for(const auto root:roots){q.Step();offer({l.protocol.value,root.value},kAbsent);}
      while(!pending.empty()){
        q.Step();const auto key=reverse?pending.back():pending.front();if(reverse)pending.pop_back();else pending.pop_front();
        const auto& l=identity.loops[key.first];const ControlNodeId node{key.second};q.witness={ForOfIterationError::kIdentity,l.protocol,{},node};
        if(const auto* unit=std::get_if<PhaseControlUnit>(&topology.mapping.units[node.value]))q.witness.operation=g.phases[unit->block.value].operation;
        auto bits=activation.find(key)->second.bits;
        q.Require(commit[l.acquisition.value] && phase[commit[l.acquisition.value]->value] && commit[l.next.value] && phase[commit[l.next.value]->value]);
        if(node==*phase[commit[l.acquisition.value]->value])bits=kFresh;
        if(node==*phase[g.ops[l.next.value].block.value])q.Require(bits==kFresh,ForOfIterationError::kIdentity);
        if(node==*phase[commit[l.next.value]->value])bits=kCached;
        if(node==out.iterations[key.first].advance)q.Require(bits==kCached,ForOfIterationError::kIdentity);
        const auto& b=topology.blocks[node.value];for(std::uint32_t e=b.first_arc.value;e<b.first_arc.value+b.arc_count;++e){q.Step();offer({key.first,topology.edges[e].target.value},bits);}
      }
    }
    // Dynamic reference proof. Walk the actual SSA operand backwards to its
    // normal defining Commit, switching to copy inputs or physical phi edges.
    // Crossing a fresh acquisition while carrying an older definition refuses;
    // a matching canonical SCC alone cannot establish the activation epoch.
    {
      std::pmr::vector<std::uint32_t> offsets(g.blocks.size()+1,0,&q.memory);
      std::pmr::vector<EdgeId> predecessors(g.edges.size(),{},&q.memory);
      std::pmr::vector<std::optional<UseId>> phi_first(g.values.size(),{},&q.memory), phi_next(g.uses.size(),{},&q.memory);
      for(const auto& e:g.edges){q.Step();++offsets[e.target.value+1];}
      for(std::size_t n=1;n<offsets.size();++n){q.Step();offsets[n]+=offsets[n-1];}
      auto cursor=std::pmr::vector<std::uint32_t>(offsets,&q.memory);
      for(std::uint32_t e=0;e<g.edges.size();++e){q.Step();predecessors[cursor[g.edges[e].target.value]++]={e};}
      for(std::uint32_t u=0;u<g.uses.size();++u){q.Step();const auto& use=g.uses[u];if(use.life!=ValueLife::kLive)continue;if(const auto* phi=std::get_if<ValueId>(&use.owner)){phi_next[u]=phi_first[phi->value];phi_first[phi->value]=UseId{u};}}
      using State=std::tuple<std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t>;
      std::pmr::set<State> seen(&q.memory);std::pmr::deque<State> worklist(&q.memory);
      // Both states and scanned predecessor/use edges are bounded. High-fanout
      // repeated queries cannot turn the admitted algorithm quadratic.
      const auto dimension=g.blocks.size()+g.edges.size()+g.values.size()+g.uses.size();
      q.Require(dimension<=SIZE_MAX/kSparseQueriesPerNode,ForOfIterationError::kResourceLimit);
      const auto limit=dimension*kSparseQueriesPerNode;std::size_t visits=0;
      const auto charge=[&](){q.Step();q.Require(visits<limit,ForOfIterationError::kResourceLimit);++visits;};
      const auto offer=[&](State state){charge();if(seen.find(state)!=seen.end())return;q.Require(seen.size()<limit,ForOfIterationError::kResourceLimit);seen.insert(state);worklist.push_back(state);};
      q.Reserve(out.references,identity.loops.size()*3);
      for(const auto& l:identity.loops) {
        for(const auto [operation,role,root]:{std::tuple{l.next,OperandRole::kBase,l.acquisition},std::tuple{l.advance,OperandRole::kCallee,l.next},std::tuple{l.advance,OperandRole::kReceiver,l.acquisition}}) {
          q.witness={ForOfIterationError::kIdentity,l.protocol,operation};
          std::optional<UseId> seed;const auto& op=g.ops[operation.value];
          for(std::uint32_t u=op.first_use.value;u<op.first_use.value+op.use_count;++u){q.Step();if(g.uses[u].role==role){q.Require(!seed,ForOfIterationError::kIdentity);seed=UseId{u};}}
          q.Require(seed.has_value(),ForOfIterationError::kIdentity);
          const auto* value=std::get_if<ValueId>(&g.uses[seed->value].operand);q.Require(value,ForOfIterationError::kIdentity);
          const auto root_value=g.ops[root.value].result;q.Require(root_value && commit[root.value],ForOfIterationError::kIdentity);
          offer({seed->value,root.value,value->value,op.block.value});bool grounded=false;
          while(!worklist.empty()) {
            charge();const auto state=reverse?worklist.back():worklist.front();if(reverse)worklist.pop_back();else worklist.pop_front();
            const ValueId current{std::get<2>(state)};const WorkBlockId node{std::get<3>(state)};
            q.Require(current.value<g.values.size() && node.value<g.blocks.size(),ForOfIterationError::kIdentity);
            q.witness.node=phase[node.value];q.witness.value=current;
            const auto& definition=g.values[current.value].origin;
            if(node==*commit[root.value] && current==*root_value){grounded=true;continue;}
            q.Require(node!=*commit[l.acquisition.value] && node!=*commit[root.value],ForOfIterationError::kIdentity);
            if(const auto* result=std::get_if<OpResult>(&definition);result && result->commit==node) {
              const auto& producer=g.ops[result->op.value];q.Require(std::holds_alternative<ValueCopy>(producer.operation) && producer.use_count==1,ForOfIterationError::kIdentity);
              const auto* source=std::get_if<ValueId>(&g.uses[producer.first_use.value].operand);q.Require(source,ForOfIterationError::kIdentity);
              offer({seed->value,root.value,source->value,producer.block.value});continue;
            }
            if(const auto* phi=std::get_if<PhiDefinition>(&definition);phi && phi->block==node) {
              bool incoming=false;
              for(auto u=phi_first[current.value];u;u=phi_next[u->value]) {
                charge();const auto& use=g.uses[u->value];q.Require(use.incoming && std::holds_alternative<EdgeId>(*use.incoming),ForOfIterationError::kIdentity);
                const auto edge=std::get<EdgeId>(*use.incoming);q.Require(edge.value<g.edges.size() && g.edges[edge.value].target==node,ForOfIterationError::kIdentity);
                const auto from=g.edges[edge.value].source;if(g.phases[from.value].reachability==PhaseReachability::kUnreachable)continue;
                const auto* source=std::get_if<ValueId>(&use.operand);q.Require(source,ForOfIterationError::kIdentity);incoming=true;offer({seed->value,root.value,source->value,from.value});
              }
              q.Require(incoming,ForOfIterationError::kIdentity);continue;
            }
            bool incoming=false;
            for(auto n=offsets[node.value];n<offsets[node.value+1];++n){charge();const auto from=g.edges[predecessors[n].value].source;if(g.phases[from.value].reachability==PhaseReachability::kUnreachable)continue;incoming=true;offer({seed->value,root.value,current.value,from.value});}
            q.Require(incoming,ForOfIterationError::kIdentity);
          }
          q.Require(grounded,ForOfIterationError::kIdentity);out.references.push_back({l.protocol,*seed,root,*value});
        }
      }
    }
    // Every ordinary protected body node must be reached AFTER this iteration's
    // value read. The body itself is never made a protocol-owned member.
    for(const auto& region:owner.Summary().regions)for(const auto& range:region.boundary->protected_ranges) {
      q.Step();if(range.role!=ProtectedRangeRole::kLoopBody)continue;
      for(const auto n:range.ordinary){q.Step();if(!raw[n.value])continue;q.witness={ForOfIterationError::kBinding,region.protocol,{},*raw[n.value]};const auto f=counts.find({region.protocol.value,raw[n.value]->value});q.Require(f!=counts.end() && f->second.bits==kValueRead,ForOfIterationError::kBinding);}
    }
    // The original SSA already separates Commit from every exception edge.
    // Preserve exact key-carrying definitions and incoming phi edges. This is
    // transport evidence, not yet a native variable/activation certificate.
    auto canonical=AnalyzeValueIdentities(g,q.Remaining());
    if(const auto* e=std::get_if<ValueSimplifyError>(&canonical))q.Fail(*e==ValueSimplifyError::kResourceLimit?ForOfIterationError::kResourceLimit:*e==ValueSimplifyError::kWorkLimit?ForOfIterationError::kWorkLimit:ForOfIterationError::kIdentity);
    auto ids=std::get<ValueIdentities>(std::move(canonical));q.memory.Retain(ids.allocated_bytes);q.Step(ids.work);
    std::pmr::vector<std::optional<IteratorProtocolId>> keys(g.values.size(),{},&q.memory);
    for(const auto& l:identity.loops){q.Step();const auto key=ids.canonical[l.key.value];q.Require(!keys[key.value]);keys[key.value]=l.protocol;}
    // Static SCC identity alone is not a dynamic key-value proof across a
    // backedge. Ground the exact SSA operands at this iteration's value read;
    // a phi needs ALL of its physical incoming values, not a canonical alias.
    // Ungrounded cyclic carriers remain missing binding evidence.
    std::pmr::vector<std::optional<IteratorProtocolId>> current(g.values.size(),{},&q.memory);
    std::pmr::vector<std::uint32_t> incoming(g.values.size(),0,&q.memory),known(g.values.size(),0,&q.memory);
    std::pmr::deque<ValueId> current_queue(&q.memory);
    for(const auto& use:g.uses){q.Step();if(use.life==ValueLife::kLive)if(const auto* phi=std::get_if<ValueId>(&use.owner))++incoming[phi->value];}
    const auto current_mark=[&](ValueId value,IteratorProtocolId protocol){q.Step();if(current[value.value]){q.Require(current[value.value]==protocol,ForOfIterationError::kIdentity);return;}current[value.value]=protocol;current_queue.push_back(value);};
    for(const auto& l:identity.loops){q.Step();q.Require(g.ops[l.value.value].result.has_value());current_mark(*g.ops[l.value.value].result,l.protocol);}
    while(!current_queue.empty()){
      q.Step();const auto value=current_queue.front();current_queue.pop_front();const auto protocol=*current[value.value];
      for(auto id=g.values[value.value].first_use;id;id=g.uses[id->value].next){q.Step();const auto& use=g.uses[id->value];if(use.life!=ValueLife::kLive)continue;
        if(const auto* phi=std::get_if<ValueId>(&use.owner)){
          if(keys[ids.canonical[phi->value].value]!=protocol)continue;
          const auto* origin=std::get_if<PhiDefinition>(&g.values[phi->value].origin);q.Require(origin);
          if(!phase[origin->block.value])continue;
          const auto f=counts.find({protocol.value,phase[origin->block.value]->value});if(f==counts.end() || f->second.bits!=kValueRead)continue;
          if(++known[phi->value]==incoming[phi->value])current_mark(*phi,protocol);
        }else{
          const auto o=std::get<OpId>(use.owner);const auto& op=g.ops[o.value];
          if(!std::holds_alternative<ValueCopy>(op.operation) || !op.result || op.use_count!=1 || !phase[op.block.value])continue;
          const auto f=counts.find({protocol.value,phase[op.block.value]->value});if(f!=counts.end() && f->second.bits==kValueRead)current_mark(*op.result,protocol);
        }
      }
    }
    q.Reserve(out.definitions,g.ops.size());q.Reserve(out.phis,g.uses.size());
    for(std::uint32_t o=0;o<g.ops.size();++o){q.Step();const auto& op=g.ops[o];if(op.life!=ValueLife::kLive || !op.result)continue;
      const auto p=keys[ids.canonical[op.result->value].value];if(!p)continue;
      q.witness={ForOfIterationError::kBinding,*p,OpId{o},{},{},op.result};
      const auto where=phase[op.block.value];
      if(!where && identity.operations[o] && identity.operations[o]->role==ForOfRole::kClose)continue;
      q.Require(where.has_value(),ForOfIterationError::kBinding);
      const auto state=counts.find({p->value,where->value});
      // Post-loop copies are ordinary live-out uses, not per-iteration binding
      // commits. Never claim them as protocol members merely by SSA identity.
      if(state==counts.end())continue;
      q.Require(state->second.bits==(OpId{o}==identity.loops[p->value].value?kAdvanced:kValueRead),ForOfIterationError::kBinding);
      q.Require(current[op.result->value]==p,ForOfIterationError::kBinding);
      q.witness.protocol=*p;q.witness.operation=OpId{o};q.Require(op.result->value<g.input.values.size());
      const auto original=g.input.values[op.result->value];q.Require(original.value<input.ssa.Values().size());
      const auto& def=input.ssa.Values()[original.value];
      const auto* site=std::get_if<InstructionValue>(&def.origin);q.Require(site && commit[o] && g.phases[commit[o]->value].input.value==site->instruction.value,ForOfIterationError::kBinding);
      // Result availability must originate in the success value event; copies
      // preserve that result, but their source-slot selection remains deferred.
      q.Require(OpId{o}==identity.loops[p->value].value || std::holds_alternative<ValueCopy>(op.operation),ForOfIterationError::kBinding);
      std::optional<abc::RegisterId> reg;if(const auto* r=std::get_if<abc::RegisterId>(&def.slot))reg=*r;
      out.definitions.push_back({*p,{o},*op.result,{original.value},reg});
    }
    std::pmr::map<std::pair<std::uint32_t,std::uint32_t>,bool> affected(&q.memory);
    std::pmr::map<Query,bool> lineage(&q.memory);std::pmr::deque<Query> lineage_queue(&q.memory);
    const auto mark=[&](Query query){q.Step();if(lineage.find(query)!=lineage.end())return;q.Require(lineage.size()<g.values.size()*kSparseQueriesPerNode,ForOfIterationError::kResourceLimit);lineage.emplace(query,true);lineage_queue.push_back(query);};
    for(const auto& l:identity.loops){q.Step();mark({l.protocol.value,l.key.value});}
    // A high-fanout SSA hub can be visited by several protocol queries. Bound
    // query-edge pairs as well as query-node pairs; a node cap alone would hide
    // quadratic propagation on dense joins. Refusal never certifies a subset.
    std::size_t lineage_visits=0;
    const auto lineage_limit=(g.values.size()+g.uses.size())*kSparseQueriesPerNode;
    while(!lineage_queue.empty()) {
      q.Step();const auto query=lineage_queue.front();lineage_queue.pop_front();
      for(auto id=g.values[query.second].first_use;id;id=g.uses[id->value].next){q.Require(lineage_visits<lineage_limit,ForOfIterationError::kResourceLimit);++lineage_visits;q.Step();const auto& use=g.uses[id->value];if(use.life!=ValueLife::kLive)continue;
        if(const auto* phi=std::get_if<ValueId>(&use.owner)){affected.emplace(std::pair{phi->value,query.first},true);mark({query.first,phi->value});}
        else {const auto& op=g.ops[std::get<OpId>(use.owner).value];if(std::holds_alternative<ValueCopy>(op.operation) && op.result)mark({query.first,op.result->value});}
      }
    }
    for(std::uint32_t u=0;u<g.uses.size();++u){q.Step();const auto& use=g.uses[u];if(use.life!=ValueLife::kLive)continue;const auto* phi=std::get_if<ValueId>(&use.owner);if(!phi)continue;
      auto p=affected.lower_bound({phi->value,0});for(;p!=affected.end() && p->first.first==phi->value;++p){q.Step();const auto* value=std::get_if<ValueId>(&use.operand);q.Require(value && use.incoming && std::holds_alternative<EdgeId>(*use.incoming),ForOfIterationError::kBinding);q.Require(out.phis.size()<out.phis.capacity(),ForOfIterationError::kResourceLimit);out.phis.push_back({{p->first.second},*phi,{u},std::get<EdgeId>(*use.incoming),*value});}
    }
    out.allocated_bytes=out.iterations.capacity()*sizeof(ForOfIteration)+out.definitions.capacity()*sizeof(ForOfKeyDefinition)+out.phis.capacity()*sizeof(ForOfKeyPhi)+out.references.capacity()*sizeof(ForOfReference);
    out.peak_bytes=q.memory.Peak();out.work=q.initial-q.left;return out;
  }catch(const ForOfIterationFailure& e){return e;}catch(ControlError e){return ForOfIterationFailure{e==ControlError::kWorkLimit?ForOfIterationError::kWorkLimit:e==ControlError::kResourceLimit?ForOfIterationError::kResourceLimit:ForOfIterationError::kCorrespondence};}
  catch(ExceptionControlError){return ForOfIterationFailure{ForOfIterationError::kCorrespondence};}
  catch(const std::bad_alloc&){return ForOfIterationFailure{ForOfIterationError::kResourceLimit};}
  catch(const std::length_error&){return ForOfIterationFailure{ForOfIterationError::kResourceLimit};}
}
}
std::variant<ForOfIterationProof,ForOfIterationFailure> ProveForOfIterations(
    const abc::MethodBody& b,const analysis::ValueProgram& g,const analysis::PhasedValueInput& i,
    const ExceptionPlan& r,const ExceptionControl& c,const VerifiedProtocolRegionOwnership& o,
    SourceContract s,const ForOfIdentityIndex& x,analysis::SemanticBudget q){return Analyze(b,g,i,r,c,o,s,x,q,false);}
std::variant<std::uint64_t,ForOfIterationFailure> VerifyForOfIterations(
    const abc::MethodBody& b,const analysis::ValueProgram& g,const analysis::PhasedValueInput& i,
    const ExceptionPlan& r,const ExceptionControl& c,const VerifiedProtocolRegionOwnership& o,
    SourceContract s,const ForOfIdentityIndex& x,const ForOfIterationProof& p,analysis::SemanticBudget q){
  auto rebuilt=Analyze(b,g,i,r,c,o,s,x,q,true);if(const auto* f=std::get_if<ForOfIterationFailure>(&rebuilt))return *f;
  const auto& expected=std::get<ForOfIterationProof>(rebuilt);const auto cost=expected.iterations.size()+expected.definitions.size()+expected.phis.size()+expected.references.size()+1;
  if(cost>q.work-expected.work)return ForOfIterationFailure{ForOfIterationError::kWorkLimit};
  if(p.revision!=g.revision || p.cfg_revision!=g.cfg_revision || p.iterations!=expected.iterations || p.definitions!=expected.definitions || p.phis!=expected.phis || p.references!=expected.references)return ForOfIterationFailure{ForOfIterationError::kCorrespondence};
  return expected.work+cost;
}
}
