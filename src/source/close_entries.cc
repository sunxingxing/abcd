#include "close_entries.h"

#include "loop_forest.h"
#include "../analysis/idf_scratch.h"
#include "../analysis/value_phi.h"
#include "abcd/abc/control_transfer.h"
#include <algorithm>
#include <bit>
#include <map>
#include <stdexcept>

namespace abcd::source {
namespace {
using namespace analysis;
struct Quota {
  detail::IdfScratch memory;
  std::uint64_t left, initial;
  explicit Quota(SemanticBudget b) : memory(b.bytes),left(b.work),initial(b.work) {}
  void Step(std::uint64_t n=1) { if(n>left) throw CloseEntryFailure{CloseEntryError::kWorkLimit};left-=n; }
  void Require(bool ok,CloseEntryError kind=CloseEntryError::kCorrespondence,
      std::optional<ExceptionNodeId> node={}) {
    if(!ok) throw CloseEntryFailure{kind,{}, {},node};
  }
  SemanticBudget Remaining() const { return {memory.Available(),left}; }
  template<class T> void Reserve(std::vector<T>& v,std::size_t n) {
    Require(n<=UINT32_MAX && n<=memory.Available()/sizeof(T),CloseEntryError::kResourceLimit);
    memory.Retain(n*sizeof(T));v.reserve(n);memory.Retain((v.capacity()-n)*sizeof(T));
  }
};
// The storage carried by a predicate's pure value dependencies is separate
// from the canonical value identity. Register copies do not invent a new
// guard state; phi storage remains an original SSA boundary.
struct Dependency {
  std::optional<abc::RegisterId> storage;
  std::optional<ExceptionNodeId> first;
  std::optional<bool> boolean;
  ValueId root;
  bool when_false=false,when_true=true,converted=false;
};
std::optional<std::pair<bool,bool>> Predicate(const Dependency& d,UnaryOperator op) {
  switch(op) {
    case UnaryOperator::kToBoolean:return {{d.when_false,d.when_true}};
    case UnaryOperator::kLogicalNot:return {{!d.when_false,!d.when_true}};
    case UnaryOperator::kFalseOrZero:
      if(d.converted)return {{!d.when_false,!d.when_true}};return {};
    case UnaryOperator::kTrueOrNonzeroNumber:
      if(d.converted)return {{d.when_false,d.when_true}};return {};
    default:return {};
  }
}
struct Guard {
  ExceptionNodeId predicate,write,first,bypass;
  abc::RegisterId storage;
};
struct Continuation {
  PendingCompletion pending;
  ExceptionNodeId origin;
  std::optional<CompletionEdge> transfer;
};
struct CallFact {
  std::uint32_t count=0,arguments=0;
  std::optional<ValueId> receiver;
};
struct AliasRange {std::uint32_t first=0,count=0;};
class Derivation {
 public:
  Derivation(const abc::MethodBody& body,abc::ProfileId profile,const ValueProgram& graph,
      const PhasedValueInput& input,const ExceptionPlan& regions,const IteratorKeys& keys,
      const IteratorProtocolScan& scan,SemanticBudget budget,bool verify)
      : body_(body),profile_(profile),g_(graph),in_(input),regions_(regions),keys_(keys),scan_(scan),q_(budget),
        before_(&q_.memory),commit_(&q_.memory),catch_(&q_.memory),copy_(&q_.memory),deps_(&q_.memory),
        ready_(&q_.memory),normal_(&q_.memory),original_(&q_.memory),guards_(&q_.memory),
        protocol_loop_(&q_.memory),protocol_advance_(&q_.memory),pred_offsets_(&q_.memory),preds_(&q_.memory),
        stamps_(&q_.memory),loop_begin_(&q_.memory),loop_end_(&q_.memory),header_loop_(&q_.memory),
        exhausted_(&q_.memory),success_(&q_.memory),value_read_(&q_.memory),value_ambiguous_(&q_.memory),calls_(&q_.memory),state_checked_(&q_.memory),
        value_stamps_(&q_.memory),guard_owner_(&q_.memory),region_offsets_(&q_.memory),region_index_(&q_.memory),
        branch_aliases_(&q_.memory),alias_slots_(&q_.memory),boundaries_(&q_.memory),
        writes_(&q_.memory),read_offsets_(&q_.memory),phi_offsets_(&q_.memory),phi_values_(&q_.memory),verify_(verify) {
    // Dense loop/entry contexts are a resource refusal, not quadratic admitted
    // work. This grant includes delegated analyses and all sparse graph walks.
    constexpr std::uint64_t kWorkPerInput=4096;
    const auto count=std::uint64_t(in_.flow.nodes.size())+g_.uses.size()+g_.values.size()+scan.regions.size()+in_.frame.registers+1;
    q_.Require(count<=UINT32_MAX,CloseEntryError::kResourceLimit);
    q_.left=q_.initial=std::min(q_.left,kWorkPerInput*count*(1+std::bit_width(count)));
    const auto checked=VerifyIteratorCandidates(g_,in_,regions_,keys,scan_,q_.Remaining());
    if(const auto* e=std::get_if<IteratorScanFailure>(&checked))
      q_.Require(false,e->kind==IteratorScanError::kResourceLimit?CloseEntryError::kResourceLimit:
          e->kind==IteratorScanError::kWorkLimit?CloseEntryError::kWorkLimit:CloseEntryError::kInput);
    q_.Step(std::get<std::uint64_t>(checked));
    // Candidate verification's grant is exclusive. Its temporary peak is
    // conservatively observed; it is gone before allocating these indexes.
    q_.memory.Observe(q_.memory.Available());
    q_.Require(g_.values.size()==in_.ssa.Values().size() &&
        g_.ops.size()==in_.flow.instructions.size(),CloseEntryError::kInput);
    auto identity=AnalyzeValueIdentities(g_,q_.Remaining());
    if(const auto* e=std::get_if<ValueSimplifyError>(&identity))
      q_.Require(false,*e==ValueSimplifyError::kResourceLimit?CloseEntryError::kResourceLimit:
          *e==ValueSimplifyError::kWorkLimit?CloseEntryError::kWorkLimit:CloseEntryError::kInput);
    identities_=std::get<ValueIdentities>(std::move(identity));
    q_.memory.Observe(identities_.allocated_bytes);q_.memory.Retain(identities_.allocated_bytes);q_.Step(identities_.work);
    Index();Dominance();Loops();Dependencies();BranchAliases();
    if(!verify_)Guards();else guards_.resize(in_.flow.nodes.size());
    ProtocolLoops();StateIndex();
  }
  CloseEntries Run(bool reverse=false) {
    CloseEntries out;q_.Reserve(out.closes,scan_.regions.size());
    for(std::size_t at=0;at<scan_.regions.size();++at) {
      const auto& region=scan_.regions[reverse?scan_.regions.size()-1-at:at];
      q_.Step();if(region.role!=IteratorRegionRole::kReturnProtection)continue;
      current_protocol_=region.protocol;current_read_=region.read.operation;
      auto close=Close(region);out.closes.push_back(std::move(close));
    }
    if(reverse)std::reverse(out.closes.begin(),out.closes.end());
    out.peak_bytes=q_.memory.Peak();out.work=q_.initial-q_.left;return out;
  }
  std::optional<IteratorProtocolId> current_protocol_;
  std::optional<OpId> current_read_;
 private:
  const ExceptionNode& Node(ExceptionNodeId n) const {
    q_.Require(n.value<in_.flow.nodes.size(),CloseEntryError::kInput,n);return in_.flow.nodes[n.value];
  }
  const SemanticInstruction& Instruction(ExceptionNodeId n) const {return in_.flow.instructions[Node(n).instruction.value];}
  bool Dominates(ExceptionNodeId a,ExceptionNodeId b) const {return dom_.Dominates({a.value},{b.value});}
  std::optional<SsaValueId> Write(ExceptionNodeId n) const {return writes_[n.value];}
  std::span<const SsaRead> Reads(ExceptionNodeId n) const {
    return in_.ssa.AllReads().subspan(read_offsets_[n.value],read_offsets_[n.value+1]-read_offsets_[n.value]);
  }
  std::span<const SsaValueId> PhiInputs(SsaValueId value) const {
    return std::span<const SsaValueId>{phi_values_}.subspan(phi_offsets_[value.value],phi_offsets_[value.value+1]-phi_offsets_[value.value]);
  }
  std::optional<SsaValueId> Read(ExceptionNodeId n,const ScalarValue& scalar) {
    std::optional<ScalarSlot> slot;
    if(std::holds_alternative<Accumulator>(scalar))slot=Accumulator{};
    else if(const auto* reg=std::get_if<abc::RegisterId>(&scalar))slot=*reg;
    if(!slot)return {};
    for(const auto& r:Reads(n)) {
      q_.Step();const auto& s=in_.ssa.Values()[r.value.value].slot;
      if(s.index()==slot->index() && (s.index()==0 || std::get<abc::RegisterId>(s)==std::get<abc::RegisterId>(*slot)))return r.value;
    }
    q_.Require(false,CloseEntryError::kPayload,n);return {};
  }
  SsaValueId Canonical(SsaValueId value) const {
    q_.Require(value.value<identities_.canonical.size(),CloseEntryError::kPayload);
    return {identities_.canonical[value.value].value};
  }
  void Index() {
    const auto& f=in_.flow;
    before_.resize(f.instructions.size());commit_.resize(f.instructions.size());catch_.resize(f.instructions.size());
    pred_offsets_.resize(f.nodes.size()+1);
    for(std::uint32_t n=0;n<f.nodes.size();++n) {
      q_.Step();const auto& v=f.nodes[n];q_.Require(v.instruction.value<f.instructions.size());
      auto& slot=v.phase==ExceptionPhase::kBefore?before_[v.instruction.value]:
        v.phase==ExceptionPhase::kCommit?commit_[v.instruction.value]:catch_[v.instruction.value];
      q_.Require(!slot);slot=ExceptionNodeId{n};
      for(std::uint8_t e=0;e<v.successor_count;++e) {q_.Step();q_.Require(v.successors[e].target.value<f.nodes.size());++pred_offsets_[v.successors[e].target.value+1];}
    }
    for(std::size_t n=1;n<pred_offsets_.size();++n){q_.Step();pred_offsets_[n]+=pred_offsets_[n-1];}
    preds_.resize(pred_offsets_.back());
    stamps_.resize(f.nodes.size());header_loop_.resize(f.nodes.size());boundaries_.resize(f.nodes.size());q_.Step(f.nodes.size()*3);
    // Raw SSA queries otherwise perform a binary search. Index them once so
    // sparse verification/region walks cannot hide another log factor.
    writes_.resize(f.nodes.size());read_offsets_.resize(f.nodes.size()+1);
    phi_offsets_.resize(in_.ssa.Values().size()+1);q_.Step(f.nodes.size()*2+in_.ssa.Values().size());
    for(std::uint32_t v=0;v<in_.ssa.Values().size();++v) {
      q_.Step();if(const auto* origin=std::get_if<InstructionValue>(&in_.ssa.Values()[v].origin)) {
        q_.Require(origin->instruction.value<f.nodes.size() && !writes_[origin->instruction.value]);
        writes_[origin->instruction.value]=SsaValueId{v};
      }
    }
    for(const auto& r:in_.ssa.AllReads()){q_.Step();q_.Require(r.instruction.value<f.nodes.size());++read_offsets_[r.instruction.value+1];}
    for(std::size_t n=1;n<read_offsets_.size();++n){q_.Step();read_offsets_[n]+=read_offsets_[n-1];}
    for(const auto& arm:in_.ssa.AllInputs()){q_.Step();q_.Require(arm.phi.value<in_.ssa.Values().size());++phi_offsets_[arm.phi.value+1];}
    for(std::size_t v=1;v<phi_offsets_.size();++v){q_.Step();phi_offsets_[v]+=phi_offsets_[v-1];}
    phi_values_.resize(phi_offsets_.back());std::pmr::vector<std::uint32_t> phi_cursor(phi_offsets_,&q_.memory);
    for(const auto& arm:in_.ssa.AllInputs()){q_.Step();phi_values_[phi_cursor[arm.phi.value]++]=arm.value;}
    std::pmr::vector<std::uint32_t> cursor(pred_offsets_,&q_.memory);
    for(std::uint32_t n=0;n<f.nodes.size();++n)for(std::uint8_t e=0;e<f.nodes[n].successor_count;++e) {
      q_.Step();preds_[cursor[f.nodes[n].successors[e].target.value]++]={{n},e};
    }
  }
  void Dominance() {
    std::pmr::vector<std::uint32_t> offsets(&q_.memory);std::pmr::vector<BlockId> edges(&q_.memory);
    offsets.push_back(0);
    for(const auto& n:in_.flow.nodes) {
      q_.Step();for(std::uint8_t e=0;e<n.successor_count;++e){q_.Step();edges.push_back({n.successors[e].target.value});}
      offsets.push_back(static_cast<std::uint32_t>(edges.size()));
    }
    auto result=BuildDominators({offsets,edges},{0},{q_.Remaining().bytes,q_.left});
    if(const auto* e=std::get_if<DominatorError>(&result))
      q_.Require(false,*e==DominatorError::kResourceLimit?CloseEntryError::kResourceLimit:
          *e==DominatorError::kWorkLimit?CloseEntryError::kWorkLimit:CloseEntryError::kInput);
    dom_=std::get<DominatorTree>(std::move(result));
    q_.memory.Observe(dom_.work.allocated_bytes);q_.memory.Retain(dom_.work.allocated_bytes);
    q_.Step(dom_.work.steps+dom_.work.edge_visits+dom_.work.eval_steps);
  }
  void Loops() {
    // Normal control and exception entry are distinct. Catch roots have their
    // own acc definition; adding a normal edge into the same PC never creates
    // a Catch. Dead Commit nodes are not fake loop entries.
    std::pmr::vector<ExceptionNodeId> roots(&q_.memory),queue(&q_.memory);
    std::pmr::vector<bool> live(in_.flow.nodes.size(),false,&q_.memory);
    roots.push_back({0});
    for(std::uint32_t n=0;n<in_.flow.nodes.size();++n) {
      q_.Step();if(in_.flow.nodes[n].phase==ExceptionPhase::kCatch && dom_.nodes[n].begin)roots.push_back({n});
    }
    for(auto root:roots){if(!live[root.value]){live[root.value]=true;queue.push_back(root);}}
    for(std::size_t i=0;i<queue.size();++i) {
      q_.Step();const auto& n=Node(queue[i]);
      for(std::uint8_t e=0;e<n.successor_count;++e) {
        q_.Step();const auto& a=n.successors[e];if(a.kind==ExceptionArcKind::kException)continue;
        if(!live[a.target.value]){live[a.target.value]=true;queue.push_back(a.target);}
      }
    }
    normal_.resize(in_.flow.nodes.size());
    for(std::uint32_t n=0;n<live.size();++n) {q_.Step();if(live[n]){normal_[n]=ControlNodeId{static_cast<std::uint32_t>(original_.size())};original_.push_back({n});}}
    std::pmr::vector<ControlVertex> nodes(&q_.memory);std::pmr::vector<ControlLink> arcs(&q_.memory);
    std::pmr::vector<ControlNodeId> entries(&q_.memory);
    for(auto root:roots)entries.push_back(*normal_[root.value]);
    for(std::uint32_t n=0;n<original_.size();++n) {
      q_.Step();const auto& raw=Node(original_[n]);ControlVertex v{{static_cast<std::uint32_t>(arcs.size())},0};
      for(std::uint8_t e=0;e<raw.successor_count;++e) {
        q_.Step();const auto& a=raw.successors[e];if(a.kind==ExceptionArcKind::kException)continue;
        q_.Require(normal_[a.target.value].has_value());
        arcs.push_back({{n},*normal_[a.target.value],a.kind==ExceptionArcKind::kTaken?FlowEdgeKind::kTaken:FlowEdgeKind::kFallthrough,v.arc_count++});
      }
      if(!v.arc_count)v.termination=ControlTermination::kAbrupt;nodes.push_back(v);
    }
    auto built=BuildLoopForest(ControlGraphView{ControlProjection{g_.revision,g_.cfg_revision,nodes,arcs}},entries,q_.Remaining());
    if(const auto* e=std::get_if<LoopError>(&built))q_.Require(false,*e==LoopError::kResourceLimit?CloseEntryError::kResourceLimit:
        *e==LoopError::kWorkLimit?CloseEntryError::kWorkLimit:CloseEntryError::kLoop);
    loops_=std::get<LoopForest>(std::move(built));q_.memory.Observe(loops_.allocated_bytes);q_.memory.Retain(loops_.allocated_bytes);q_.Step(loops_.work);
    // Parent-linked intervals answer containment/outer-continue queries in
    // constant time, including deeply nested loops; no per-entry parent walk.
    const auto count=loops_.loops.size();loop_begin_.resize(count);loop_end_.resize(count);
    std::pmr::vector<std::uint32_t> child_offsets(count+2,0,&q_.memory);
    for(std::uint32_t l=0;l<count;++l) {
      q_.Step();const auto parent=loops_.loops[l].parent;
      ++child_offsets[(parent?parent->value:count)+1];
      header_loop_[original_[loops_.loops[l].header.value].value]=LoopId{l};
    }
    for(std::size_t l=1;l<child_offsets.size();++l){q_.Step();child_offsets[l]+=child_offsets[l-1];}
    std::pmr::vector<LoopId> children(count,{},&q_.memory);
    std::pmr::vector<std::uint32_t> position(child_offsets,&q_.memory);
    for(std::uint32_t l=0;l<count;++l){q_.Step();const auto p=loops_.loops[l].parent;children[position[p?p->value:count]++]={l};}
    struct Visit{LoopId loop;bool leave;};std::pmr::vector<Visit> stack(&q_.memory);
    for(auto l=child_offsets[count];l<child_offsets[count+1];++l)stack.push_back({children[l],false});
    std::uint32_t stamp=0;
    while(!stack.empty()) {
      q_.Step();const auto v=stack.back();stack.pop_back();
      if(v.leave){loop_end_[v.loop.value]=stamp;continue;}
      loop_begin_[v.loop.value]=stamp++;stack.push_back({v.loop,true});
      for(auto i=child_offsets[v.loop.value];i<child_offsets[v.loop.value+1];++i)stack.push_back({children[i],false});
    }
  }
  void Dependencies() {
    const auto count=in_.ssa.Values().size();deps_.resize(count);ready_.resize(count);copy_.resize(count);
    struct Visit{SsaValueId id;bool leave;};std::pmr::vector<Visit> stack(&q_.memory);
    for(std::uint32_t v=0;v<count;++v) {
      stack.push_back({{v},false});
      while(!stack.empty()) {
        q_.Step();const auto visit=stack.back();stack.pop_back();const auto id=visit.id.value;
        if(ready_[id]==2)continue;
        const auto& value=in_.ssa.Values()[id];
        const auto* def=std::get_if<InstructionValue>(&value.origin);
        const ValueOp* op=nullptr;
        if(def && Node({def->instruction.value}).phase==ExceptionPhase::kCommit)
          op=&g_.ops[Node({def->instruction.value}).instruction.value];
        if(!visit.leave) {
          if(ready_[id]==1) {q_.Require(false,CloseEntryError::kInput);}
          ready_[id]=1;stack.push_back({visit.id,true});
          if(op && (std::holds_alternative<ValueCopy>(op->operation)||std::holds_alternative<ValueUnary>(op->operation))) {
            q_.Require(op->use_count==1);const auto& use=g_.uses[op->first_use.value];
            if(const auto* source=std::get_if<ValueId>(&use.operand)) {
              copy_[id]=SsaValueId{source->value};stack.push_back({*copy_[id],false});
            }
          }
          continue;
        }
        Dependency d;d.root=ValueId{id};
        if(copy_[id])d=deps_[copy_[id]->value];
        if(op && std::holds_alternative<ValueCopy>(op->operation)) {
          const auto& operand=g_.uses[op->first_use.value].operand;
          if(const auto* s=std::get_if<SpecialValue>(&operand)) {
            if(*s==SpecialValue::kTrue)d.boolean=true;
            else if(*s==SpecialValue::kFalse)d.boolean=false;
          }
        } else if(op) {
          if(const auto* unary=std::get_if<ValueUnary>(&op->operation)) {
            if(const auto truth=Predicate(d,unary->op)) {
              d.when_false=truth->first;d.when_true=truth->second;d.converted=true;
              if(d.boolean)d.boolean=(unary->op==UnaryOperator::kLogicalNot || unary->op==UnaryOperator::kFalseOrZero)?!*d.boolean:*d.boolean;
            } else {d.root=ValueId{id};d.converted=false;d.boolean.reset();d.storage.reset();d.first.reset();}
          }
        }
        if(const auto* reg=std::get_if<abc::RegisterId>(&value.slot);reg && !d.storage) {
          d.storage=*reg;
          if(d.boolean || std::holds_alternative<PhiValue>(value.origin)) {
            d.when_false=false;d.when_true=true;d.converted=true;
          }
        }
        if(op && copy_[id] && std::holds_alternative<Accumulator>(value.slot) &&
            std::holds_alternative<abc::RegisterId>(in_.ssa.Values()[copy_[id]->value].slot))
          d.first=before_[Node({def->instruction.value}).instruction.value];
        deps_[id]=d;ready_[id]=2;
      }
    }
  }
  void Guards() {
    struct Visit{ExceptionNodeId node;bool leave;};
    std::pmr::vector<std::uint32_t> offsets(in_.flow.nodes.size()+1,0,&q_.memory);
    for(const auto& n:dom_.nodes)if(n.parent)++offsets[n.parent->value+1];
    for(std::size_t i=1;i<offsets.size();++i)offsets[i]+=offsets[i-1];
    std::pmr::vector<ExceptionNodeId> children(offsets.back(),{},&q_.memory);
    std::pmr::vector<std::uint32_t> cursor(offsets,&q_.memory);
    for(std::uint32_t n=0;n<dom_.nodes.size();++n)if(dom_.nodes[n].parent)children[cursor[dom_.nodes[n].parent->value]++]={n};
    std::pmr::vector<std::optional<ExceptionNodeId>> branches(in_.frame.registers,{},&q_.memory);
    struct Change{abc::RegisterId slot;std::optional<ExceptionNodeId> before;};
    std::pmr::vector<Change> changes(&q_.memory);
    std::pmr::vector<std::size_t> restore(in_.flow.nodes.size(),0,&q_.memory);
    std::pmr::vector<Guard> active(&q_.memory);std::pmr::vector<bool> pushed(in_.flow.nodes.size(),false,&q_.memory);
    std::pmr::vector<Visit> visits(&q_.memory);visits.push_back({{0},false});guards_.resize(in_.flow.nodes.size());
    while(!visits.empty()) {
      q_.Step();const auto [id,leave]=visits.back();visits.pop_back();const auto& n=Node(id);
      if(leave) {
        if(pushed[id.value])active.pop_back();
        while(changes.size()>restore[id.value]){const auto change=changes.back();changes.pop_back();branches[change.slot.value]=change.before;}continue;
      }
      restore[id.value]=changes.size();
      visits.push_back({id,true});
      if(n.phase==ExceptionPhase::kCommit) {
        if(const auto* b=std::get_if<BranchOperation>(&Instruction(id).operation)) {
          const auto value=Read(*before_[n.instruction.value],b->predicate.input);
          if(value)for(const auto slot:Aliases(id)){q_.Step();changes.push_back({slot,branches[slot.value]});branches[slot.value]=id;}
        }
        if(const auto value=Write(id)) {
          const auto* slot=std::get_if<abc::RegisterId>(&in_.ssa.Values()[value->value].slot);
          if(slot && deps_[value->value].boolean==true && branches[slot->value]) {
            const auto branch=*branches[slot->value];const auto& b=Node(branch);
            q_.Require(b.successor_count==2,CloseEntryError::kGuard,branch);
            std::optional<ExceptionNodeId> bypass;
            unsigned controlling=0;
            for(std::uint8_t e=0;e<b.successor_count;++e) {
              if(Dominates(b.successors[e].target,id))++controlling;
              else bypass=b.successors[e].target;
            }
            const auto& predicate=std::get<BranchOperation>(Instruction(branch).operation);
            const auto input=Read(*before_[Node(branch).instruction.value],predicate.predicate.input);
            const auto truth=input?Predicate(deps_[input->value],predicate.predicate.op):std::nullopt;
            const bool bypass_taken=bypass && b.successors[0].target==*bypass ?
                b.successors[0].kind==ExceptionArcKind::kTaken : b.successors[1].kind==ExceptionArcKind::kTaken;
            if(controlling==1 && bypass && input && deps_[input->value].first && truth &&
                truth->first!=truth->second && truth->second==bypass_taken) {
              active.push_back({branch,id,*deps_[input->value].first,*bypass,*slot});pushed[id.value]=true;
            }
          }
        }
      }
      if(!active.empty())guards_[id.value]=active.back();
      for(auto i=offsets[id.value+1];i>offsets[id.value];--i)visits.push_back({children[i-1],false});
    }
  }
  std::span<const abc::RegisterId> Aliases(ExceptionNodeId branch) const {
    const auto range=branch_aliases_[branch.value];return std::span<const abc::RegisterId>{alias_slots_}.subspan(range.first,range.count);
  }
  bool Aliased(ExceptionNodeId branch,abc::RegisterId slot) {
    const auto values=Aliases(branch);
    return std::binary_search(values.begin(),values.end(),slot,[&](auto a,auto b){q_.Step();return a<b;});
  }
  void BranchAliases() {
    branch_aliases_.resize(in_.flow.nodes.size());q_.Step(in_.flow.nodes.size());
    std::pmr::vector<std::uint32_t> marks(in_.frame.registers,0,&q_.memory);q_.Step(in_.frame.registers);
    std::uint32_t generation=0;
    for(std::uint32_t n=0;n<in_.flow.nodes.size();++n) {
      q_.Step();if(Node({n}).phase!=ExceptionPhase::kCommit)continue;
      const auto* b=std::get_if<BranchOperation>(&Instruction({n}).operation);if(!b)continue;
      auto value=Read(*before_[Node({n}).instruction.value],b->predicate.input);if(!value)continue;
      q_.Require(++generation!=0,CloseEntryError::kResourceLimit);
      const auto start=alias_slots_.size();
      while(value) {
        q_.Step();const auto* slot=std::get_if<abc::RegisterId>(&in_.ssa.Values()[value->value].slot);
        if(slot && marks[slot->value]!=generation){marks[slot->value]=generation;alias_slots_.push_back(*slot);}
        value=copy_[value->value];
      }
      std::sort(alias_slots_.begin()+start,alias_slots_.end(),[&](auto a,auto b){q_.Step();return a<b;});
      q_.Require(alias_slots_.size()<=UINT32_MAX,CloseEntryError::kResourceLimit);
      branch_aliases_[n]={static_cast<std::uint32_t>(start),static_cast<std::uint32_t>(alias_slots_.size()-start)};
    }
  }
  void ProtocolLoops() {
    protocol_loop_.resize(scan_.candidates.size());protocol_advance_.resize(scan_.candidates.size());
    exhausted_.resize(scan_.candidates.size());success_.resize(scan_.candidates.size());value_read_.resize(scan_.candidates.size());value_ambiguous_.resize(scan_.candidates.size());
    calls_.resize(g_.values.size());q_.Step(g_.values.size());
    std::pmr::vector<std::optional<IteratorProtocolId>> callee(g_.values.size(),{},&q_.memory);
    for(const auto& c:scan_.candidates)if(c.next) {
      const auto& read=g_.ops[c.next->value];q_.Require(read.result.has_value(),CloseEntryError::kAcquisition);
      callee[Canonical({read.result->value}).value]=c.id;
    }
    for(const auto& op:g_.ops) {
      q_.Step();if(op.life!=ValueLife::kLive || !std::holds_alternative<ValueInvoke>(op.operation))continue;
      std::optional<ValueId> function,receiver;
      for(std::uint32_t u=0;u<op.use_count;++u) {
        q_.Step();const auto& use=g_.uses[op.first_use.value+u];
        if(const auto* value=std::get_if<ValueId>(&use.operand)) {
          if(use.role==OperandRole::kCallee)function=*value;
          if(use.role==OperandRole::kReceiver)receiver=*value;
        }
      }
      if(!function)continue;
      const auto canonical=Canonical({function->value});auto& call=calls_[canonical.value];
      ++call.count;call.arguments=std::get<ValueInvoke>(op.operation).arguments;
      call.receiver=receiver?std::optional{ValueId{Canonical({receiver->value}).value}}:std::nullopt;
      const auto protocol=callee[canonical.value];if(!protocol)continue;
      q_.Require(receiver && Canonical({receiver->value}).value==scan_.candidates[protocol->value].iterator.value,CloseEntryError::kAcquisition);
      const auto raw=g_.phases[op.block.value].input;q_.Require(normal_[raw.value].has_value(),CloseEntryError::kLoop,raw);
      const auto loop=loops_.owners[normal_[raw.value]->value];
      q_.Require(!protocol_advance_[protocol->value],CloseEntryError::kLoop,raw);
      protocol_loop_[protocol->value]=loop;protocol_advance_[protocol->value]=raw;
    }
    std::pmr::vector<std::optional<IteratorProtocolId>> results(g_.values.size(),{},&q_.memory),done(g_.values.size(),{},&q_.memory);
    for(std::size_t p=0;p<protocol_advance_.size();++p) {
      q_.Require(protocol_advance_[p].has_value(),CloseEntryError::kLoop);
      const auto& op=g_.ops[Node(*protocol_advance_[p]).instruction.value];q_.Require(op.result.has_value());
      results[Canonical({op.result->value}).value]=IteratorProtocolId{static_cast<std::uint32_t>(p)};
    }
    for(const auto& key:keys_.uses) {
      q_.Step();const auto kind=keys_.origins[key.origin].key;
      if(kind!=IteratorKey::kDone && kind!=IteratorKey::kValue)continue;
      const auto& op=g_.ops[key.operation.value];
      for(std::uint32_t u=0;u<op.use_count;++u) {
        q_.Step();const auto& use=g_.uses[op.first_use.value+u];
        if(use.role!=OperandRole::kBase)continue;
        if(const auto* base=std::get_if<ValueId>(&use.operand))
          if(const auto p=results[Canonical({base->value}).value]) {
            q_.Require(op.result.has_value());
            if(kind==IteratorKey::kDone)done[Canonical({op.result->value}).value]=p;
            else {
              const auto before=g_.phases[op.block.value].input;
              // Duplicated value observations cannot define a unique body entry.
              if(value_read_[p->value] && *value_read_[p->value]!=before)value_ambiguous_[p->value]=true;
              else value_read_[p->value]=before;
            }
          }
      }
    }
    for(std::uint32_t n=0;n<in_.flow.nodes.size();++n) {
      q_.Step();if(Node({n}).phase!=ExceptionPhase::kCommit)continue;
      const auto* b=std::get_if<BranchOperation>(&Instruction({n}).operation);if(!b)continue;
      const auto value=Read(*before_[Node({n}).instruction.value],b->predicate.input);if(!value)continue;
      const auto& d=deps_[value->value];const auto p=done[Canonical({d.root.value}).value];if(!p)continue;
      const auto truth=Predicate(d,b->predicate.op);
      q_.Require(truth && truth->first!=truth->second && !exhausted_[p->value],CloseEntryError::kLoop,ExceptionNodeId{n});
      const auto& node=Node({n});q_.Require(node.successor_count==2,CloseEntryError::kLoop,ExceptionNodeId{n});
      for(std::uint8_t e=0;e<node.successor_count;++e) {
        q_.Step();const auto& edge=node.successors[e];const bool taken=edge.kind==ExceptionArcKind::kTaken;
        if(taken==truth->second)exhausted_[p->value]=CompletionEdge{{n},e};
        else success_[p->value]=edge.target;
      }
    }
  }
  bool Inside(LoopId loop,ExceptionNodeId n) {
    if(!normal_[n.value])return false;
    const auto owner=loops_.owners[normal_[n.value]->value];q_.Step();
    return owner && loop_begin_[loop.value]<=loop_begin_[owner->value] && loop_begin_[owner->value]<loop_end_[loop.value];
  }
  std::uint32_t Stamp() {q_.Require(generation_!=UINT32_MAX,CloseEntryError::kResourceLimit);return ++generation_;}
  Guard GuardFor(ExceptionNodeId read) {
    if(guards_[read.value])return *guards_[read.value];
    q_.Require(verify_,CloseEntryError::kGuard,read);
    // Verifier discovery walks immutable dominance ancestors from the read,
    // rather than trusting the producer's forward mutable guard stacks.
    // Every step consumes the same N log N admission grant; a dense set of
    // independent contexts refuses instead of running an unbounded walk.
    auto cursor=dom_.nodes[read.value].parent;
    while(cursor) {
      q_.Step();const ExceptionNodeId write{cursor->value};const auto value=Write(write);
      if(value && Node(write).phase==ExceptionPhase::kCommit && deps_[value->value].boolean==true) {
        const auto* slot=std::get_if<abc::RegisterId>(&in_.ssa.Values()[value->value].slot);
        if(slot) {
          auto ancestor=dom_.nodes[write.value].parent;
          while(ancestor) {
            q_.Step();const ExceptionNodeId branch{ancestor->value};
            const auto* b=Node(branch).phase==ExceptionPhase::kCommit?std::get_if<BranchOperation>(&Instruction(branch).operation):nullptr;
            if(b) {
              const auto input=Read(*before_[Node(branch).instruction.value],b->predicate.input);
              if(input && Aliased(branch,*slot)) {
                const auto truth=Predicate(deps_[input->value],b->predicate.op);const auto& n=Node(branch);
                unsigned controlled=0;std::optional<ExceptionNodeId> bypass;
                bool taken=false;
                for(std::uint8_t e=0;e<n.successor_count;++e) {
                  q_.Step();if(Dominates(n.successors[e].target,write))++controlled;
                  else {bypass=n.successors[e].target;taken=n.successors[e].kind==ExceptionArcKind::kTaken;}
                }
                if(controlled==1 && bypass && truth && truth->first!=truth->second && truth->second==taken && deps_[input->value].first) {
                  Guard found{branch,write,*deps_[input->value].first,*bypass,*slot};guards_[read.value]=found;return found;
                }
                break;
              }
            }
            ancestor=dom_.nodes[ancestor->value].parent;
          }
        }
      }
      cursor=dom_.nodes[cursor->value].parent;
    }
    q_.Require(false,CloseEntryError::kGuard,read);return {};
  }
  void StateIndex() {
    state_checked_.resize(scan_.candidates.size());value_stamps_.resize(g_.values.size());
    guard_owner_.resize(in_.flow.nodes.size());region_offsets_.resize(scan_.candidates.size()+1);
    q_.Step(g_.values.size()+in_.flow.nodes.size()+scan_.candidates.size()*2);
    for(const auto& r:scan_.regions)if(r.role==IteratorRegionRole::kReturnProtection) {
      q_.Step();++region_offsets_[r.protocol.value+1];const auto guard=GuardFor(r.read.before);
      for(const auto n:{guard.predicate,guard.write}) {
        q_.Require(!guard_owner_[n.value] || guard_owner_[n.value]==r.protocol,CloseEntryError::kGuard,n);guard_owner_[n.value]=r.protocol;
      }
    }
    for(std::size_t p=1;p<region_offsets_.size();++p){q_.Step();region_offsets_[p]+=region_offsets_[p-1];}
    region_index_.resize(region_offsets_.back());std::pmr::vector<std::uint32_t> cursor(region_offsets_,&q_.memory);
    for(std::uint32_t r=0;r<scan_.regions.size();++r) {
      q_.Step();const auto& value=scan_.regions[r];if(value.role==IteratorRegionRole::kReturnProtection)region_index_[cursor[value.protocol.value]++]=r;
    }
  }
  void Between(ExceptionNodeId begin,ExceptionNodeId end,abc::RegisterId storage,
      bool allow_true,bool pure) {
    q_.Require(Dominates(begin,end),CloseEntryError::kGuard,end);
    const auto stamp=Stamp();std::pmr::vector<ExceptionNodeId> queue(&q_.memory);queue.push_back(end);
    for(std::size_t i=0;i<queue.size();++i) {
      q_.Step();const auto node=queue[i];if(node==begin || stamps_[node.value]==stamp)continue;stamps_[node.value]=stamp;
      if(pure) {
        const auto& effects=Instruction(node).effects;
        q_.Require(effects.exceptional==ExceptionalCompletion::kImpossible && effects.normal==LanguageEffect::kNone &&
            effects.before_throw==LanguageEffect::kNone,CloseEntryError::kGuard,node);
      }
      if(const auto write=Write(node)) {
        if(const auto* slot=std::get_if<abc::RegisterId>(&in_.ssa.Values()[write->value].slot);slot && *slot==storage)
          q_.Require(allow_true && deps_[write->value].boolean==true,CloseEntryError::kGuard,node);
      }
      for(auto p=pred_offsets_[node.value];p<pred_offsets_[node.value+1];++p){q_.Step();queue.push_back(preds_[p].source);}
    }
  }
  void State(const IteratorRegionCandidate& region,const Guard& guard,ExceptionNodeId acquisition,ExceptionNodeId advance) {
    if(!state_checked_[region.protocol.value]) {
    std::pmr::vector<SsaValueId> queue(&q_.memory);
    const auto stamp=Stamp();
    for(auto p=region_offsets_[region.protocol.value];p<region_offsets_[region.protocol.value+1];++p) {
      q_.Step();const auto& r=scan_.regions[region_index_[p]];const auto candidate=GuardFor(r.read.before);
      q_.Require(candidate.storage==guard.storage,CloseEntryError::kGuard,r.read.before);
      const auto reads=Reads(candidate.first);
      q_.Require(reads.size()==1,CloseEntryError::kGuard,candidate.first);
      queue.push_back(reads[0].value);queue.push_back(*Write(candidate.write));
    }
    std::optional<ExceptionNodeId> initialized;
    for(std::size_t i=0;i<queue.size();++i) {
      q_.Step();const auto id=queue[i];if(value_stamps_[id.value]==stamp)continue;value_stamps_[id.value]=stamp;
      const auto& value=in_.ssa.Values()[id.value];
      if(std::holds_alternative<PhiValue>(value.origin)) {
        for(const auto arm:PhiInputs(id)){q_.Step();queue.push_back(arm);}continue;
      }
      const auto* origin=std::get_if<InstructionValue>(&value.origin);
      q_.Require(origin && Node({origin->instruction.value}).phase==ExceptionPhase::kCommit,CloseEntryError::kGuard);
      const ExceptionNodeId node{origin->instruction.value};
      const auto* slot=std::get_if<abc::RegisterId>(&value.slot);
      if(slot && *slot==guard.storage && deps_[id.value].boolean) {
        q_.Require(Dominates(acquisition,node),CloseEntryError::kGuard,node);
        if(!*deps_[id.value].boolean) {
          q_.Require(Dominates(node,advance),CloseEntryError::kGuard,node);
          // Several pure writes of false before the first advance can be
          // harmless; none may reset the live state after iteration starts.
          initialized=node;
        } else q_.Require(guard_owner_[node.value]==region.protocol,CloseEntryError::kGuard,node);
      } else {
        q_.Require(copy_[id.value].has_value() && std::holds_alternative<ValueCopy>(g_.ops[Node(node).instruction.value].operation),CloseEntryError::kGuard,node);
        queue.push_back(*copy_[id.value]);
      }
    }
    q_.Require(initialized.has_value(),CloseEntryError::kGuard,guard.first);
    // Closed state is private control, not an observable user boolean. Follow
    // real SSA use links through transport/phis to the certified predicates.
    const auto forward=Stamp();
    for(std::size_t i=0;i<queue.size();++i) {
      q_.Step();const auto id=queue[i];if(value_stamps_[id.value]==forward)continue;value_stamps_[id.value]=forward;
      for(auto use=g_.values[id.value].first_use;use;use=g_.uses[use->value].next) {
        q_.Step();const auto& u=g_.uses[use->value];
        if(const auto* phi=std::get_if<ValueId>(&u.owner)){queue.push_back({phi->value});continue;}
        const auto op=std::get<OpId>(u.owner);const auto& value=g_.ops[op.value];
        if(std::holds_alternative<ValueCopy>(value.operation) || std::holds_alternative<ValueUnary>(value.operation)) {
          q_.Require(value.result.has_value(),CloseEntryError::kGuard);
          if(const auto* unary=std::get_if<ValueUnary>(&value.operation))q_.Require(Predicate(deps_[id.value],unary->op).has_value(),CloseEntryError::kGuard);
          queue.push_back({value.result->value});
        } else if(std::holds_alternative<ValueBranch>(value.operation)) {
          q_.Require(value.block.value<g_.phases.size(),CloseEntryError::kInput);
          const auto before=g_.phases[value.block.value].input;const auto commit=commit_[Node(before).instruction.value];
          q_.Require(commit && guard_owner_[commit->value]==region.protocol,CloseEntryError::kGuard,before);
        } else q_.Require(false,CloseEntryError::kGuard,ExceptionNodeId{value.block.value});
      }
    }
    state_checked_[region.protocol.value]=true;
    }
    const auto root_reads=Reads(guard.first);q_.Require(root_reads.size()==1,CloseEntryError::kGuard,guard.first);
    auto version=root_reads[0].value;auto snapshot=guard.first;
    for(;;) {
      q_.Step();const auto& value=in_.ssa.Values()[version.value];
      const auto* slot=std::get_if<abc::RegisterId>(&value.slot);
      if(slot && *slot==guard.storage)break;
      const auto* origin=std::get_if<InstructionValue>(&value.origin);
      q_.Require(origin && copy_[version.value],CloseEntryError::kGuard,guard.first);
      snapshot=*before_[Node({origin->instruction.value}).instruction.value];version=*copy_[version.value];
    }
    // A register alias is allowed, but not a stale snapshot retained across a
    // previous close. Reprove availability on the original exception graph.
    Between(snapshot,guard.first,guard.storage,false,false);
    Between(guard.first,guard.predicate,guard.storage,false,true);
    // Reaching the return getter must not bypass the true update through a
    // redefinition of the closed state. Walk original predecessors (including
    // pre-throw snapshots), never infer this from physical instruction order.
    Between(guard.write,region.read.before,guard.storage,true,false);
    // The return method must be called on this exact iterator, not on the
    // flag, the next-result object, or a separately created iterator.
    const auto result=g_.ops[region.read.operation.value].result;q_.Require(result.has_value(),CloseEntryError::kAcquisition);
    const auto& call=calls_[Canonical({result->value}).value];
    q_.Require(call.count==1 && call.arguments==0 && call.receiver &&
        call.receiver->value==scan_.candidates[region.protocol.value].iterator.value,CloseEntryError::kAcquisition,region.read.before);
  }
  ExceptionNodeId Boundary(ExceptionNodeId n) {
    const auto stamp=Stamp();std::pmr::vector<ExceptionNodeId> path(&q_.memory);ExceptionNodeId result;
    for(;;) {
      q_.Step();if(boundaries_[n.value]){result=*boundaries_[n.value];break;}
      q_.Require(stamps_[n.value]!=stamp,CloseEntryError::kContinuation,n);stamps_[n.value]=stamp;
      path.push_back(n);const auto& node=Node(n);const auto& effect=Instruction(n).effects;
      const auto transfer=abc::GetControlTransfer(body_.instructions[node.instruction.value].decoded.opcode);
      const auto& registers=effect.normal_registers;
      if(!transfer || (*transfer!=abc::ControlTransfer::kFallthrough && *transfer!=abc::ControlTransfer::kJump) ||
          effect.exceptional!=ExceptionalCompletion::kImpossible || effect.normal!=LanguageEffect::kNone ||
          effect.before_throw!=LanguageEffect::kNone || registers.read_count || registers.range || registers.write ||
          node.successor_count!=1) {result=n;break;}
      n=node.successors[0].target;
    }
    for(auto node:path){q_.Step();boundaries_[node.value]=result;}return result;
  }
  Continuation Continue(ExceptionNodeId n,std::optional<LoopId> loop,ExceptionNodeId exhausted,CompletionEdge incoming) {
    const auto stamp=Stamp();
    for(;;) {
      q_.Step();q_.Require(stamps_[n.value]!=stamp,CloseEntryError::kContinuation,n);stamps_[n.value]=stamp;
      const auto& node=Node(n);const auto& instruction=Instruction(n);
      if(n==exhausted)
        return {{PendingCompletionKind::kBreak,{},n},incoming.source,incoming};
      if(node.phase==ExceptionPhase::kBefore) {
        if(const auto* ret=std::get_if<ReturnOperation>(&instruction.operation)) {
          CompletionPayload payload;
          if(const auto value=Read(n,ret->value))payload=Canonical(*value);
          else q_.Require(std::get_if<SpecialValue>(&ret->value) && std::get<SpecialValue>(ret->value)==SpecialValue::kUndefined,CloseEntryError::kPayload,n);
          return {{PendingCompletionKind::kReturn,payload,CompletionCaller{}},n,{}};
        }
        if(const auto* thrown=std::get_if<ThrowOperation>(&instruction.operation)) {
          const auto value=Read(n,thrown->value);q_.Require(value.has_value(),CloseEntryError::kPayload,n);
          return {{PendingCompletionKind::kThrow,Canonical(*value),CompletionCaller{}},n,{}};
        }
      }
      q_.Require(instruction.effects.exceptional==ExceptionalCompletion::kImpossible &&
          instruction.effects.normal==LanguageEffect::kNone && instruction.effects.before_throw==LanguageEffect::kNone,
          CloseEntryError::kContinuation,n);
      q_.Require(node.successor_count==1,CloseEntryError::kContinuation,n);
      const auto next=node.successors[0].target;
      if(node.phase==ExceptionPhase::kCommit && std::holds_alternative<JumpOperation>(instruction.operation) && loop && !Inside(*loop,next)) {
        const auto outer=header_loop_[next.value];
        if(outer && loop_begin_[outer->value]<loop_begin_[loop->value] && loop_begin_[loop->value]<loop_end_[outer->value])
          return {{PendingCompletionKind::kContinue,{},next},n,CompletionEdge{n,0}};
      }
      incoming={n,0};n=next;
    }
  }
  void Exits(DerivedClose& close,TryId internal) {
    const auto stamp=Stamp();
    std::pmr::vector<ExceptionNodeId> queue(&q_.memory);
    for(const auto& entry:close.entries) {
      const auto& source=Node(entry.entry.edge.source);
      const auto target=source.successors[entry.entry.edge.successor].target;
      if(stamps_[target.value]!=stamp){stamps_[target.value]=stamp;queue.push_back(target);}
    }
    // No dense per-close bit matrix survives; work is globally prepaid. Each
    // reachable edge is visited once in this sparse close query.
    std::pmr::vector<CompletionExit> cuts(&q_.memory);
    const auto target=close.entries.front().entry.pending.target;
    for(std::size_t i=0;i<queue.size();++i) {
      q_.Step();const auto n=queue[i];const auto& node=Node(n);const auto& instruction=Instruction(n);
      if((node.exception && !node.exception->target) ||
          (node.phase==ExceptionPhase::kCommit && std::holds_alternative<ReturnOperation>(instruction.operation)))
        cuts.push_back(CompletionCallerExit{n});
      for(std::uint8_t e=0;e<node.successor_count;++e) {
        q_.Step();const auto& edge=node.successors[e];
        const bool external=edge.kind==ExceptionArcKind::kException &&
          (!node.exception || !node.exception->target || node.exception->target->region!=internal);
        const bool completion=edge.kind!=ExceptionArcKind::kException && std::holds_alternative<ExceptionNodeId>(target) &&
          edge.target==std::get<ExceptionNodeId>(target);
        if(external || completion)cuts.push_back(CompletionEdge{n,e});
        else if(stamps_[edge.target.value]!=stamp){stamps_[edge.target.value]=stamp;queue.push_back(edge.target);}
      }
    }
    q_.Reserve(close.exits,cuts.size());close.exits.assign(cuts.begin(),cuts.end());
  }
  DerivedClose Close(const IteratorRegionCandidate& region) {
    q_.Require(region.protocol.value<protocol_loop_.size() && protocol_advance_[region.protocol.value].has_value(),CloseEntryError::kLoop);
    const auto loop=protocol_loop_[region.protocol.value];const auto read=region.read.before;
    const auto guard=GuardFor(read);
    const auto& acquisition=scan_.candidates[region.protocol.value].acquisition;
    const auto acquisition_commit=commit_[Node(acquisition.before).instruction.value];
    q_.Require(acquisition_commit && Dominates(*acquisition_commit,guard.first) &&
        Dominates(guard.first,guard.predicate) && Dominates(guard.write,read),CloseEntryError::kAcquisition,read);
    q_.Require(!loop || !Inside(*loop,acquisition.before),CloseEntryError::kAcquisition,acquisition.before);
    const auto advance=*protocol_advance_[region.protocol.value];
    const auto advance_commit=commit_[Node(advance).instruction.value];
    q_.Require(advance_commit && Dominates(*advance_commit,guard.first),CloseEntryError::kAcquisition,guard.first);
    q_.Require(exhausted_[region.protocol.value] && success_[region.protocol.value],CloseEntryError::kLoop,advance);
    const auto exhausted=*exhausted_[region.protocol.value];const auto success=*success_[region.protocol.value];
    const auto exit=Boundary(Node(exhausted.source).successors[exhausted.successor].target);
    q_.Require(Dominates(success,guard.first),CloseEntryError::kExternalEntry,guard.first);
    State(region,guard,*acquisition_commit,advance);
    std::uint8_t bypass_edge=0;
    while(bypass_edge<Node(guard.predicate).successor_count && Node(guard.predicate).successors[bypass_edge].target!=guard.bypass)++bypass_edge;
    q_.Require(bypass_edge<Node(guard.predicate).successor_count,CloseEntryError::kGuard);
    const auto pending=Continue(guard.bypass,loop,exit,{guard.predicate,bypass_edge});
    DerivedClose close{region.protocol,region.read.operation,
        {advance,exhausted,exit,success,loop?std::optional{original_[loops_.loops[loop->value].header.value]}:std::nullopt},guard.predicate,guard.write};
    if(!value_ambiguous_[region.protocol.value])close.iteration.value=value_read_[region.protocol.value];
    if(pending.pending.kind==PendingCompletionKind::kThrow) {
      const auto payload=std::get<SsaValueId>(pending.pending.payload);
      const auto* origin=std::get_if<InstructionValue>(&in_.ssa.Values()[payload.value].origin);
      q_.Require(origin && Node({origin->instruction.value}).phase==ExceptionPhase::kCatch,CloseEntryError::kCatch,pending.origin);
      const ExceptionNodeId catcher{origin->instruction.value};
      bool associated=false;
      for(const auto& r:scan_.regions) {
        q_.Step();if(r.protocol!=region.protocol || r.read!=region.read || r.role!=IteratorRegionRole::kEnclosingHandler)continue;
        associated|=regions_.layout.regions[r.region.value].handler.begin==Node(catcher).instruction;
      }
      q_.Require(associated && Dominates(catcher,guard.first),CloseEntryError::kCatch,catcher);
      q_.Reserve(close.entries,pred_offsets_[catcher.value+1]-pred_offsets_[catcher.value]);
      for(auto i=pred_offsets_[catcher.value];i<pred_offsets_[catcher.value+1];++i) {
        q_.Step();const auto edge=preds_[i];const auto& source=Node(edge.source);
        // The body handler can also be entered by an error from an inlined
        // close copy. Its CURRENT exception is the pending Throw here. Do not
        // substitute the earlier Break/Return or require natural-loop
        // membership of the inner Catch's private exit arm.
        q_.Require(source.successors[edge.successor].kind==ExceptionArcKind::kException &&
            Dominates(*acquisition_commit,edge.source) && Dominates(success,edge.source),CloseEntryError::kExternalEntry,edge.source);
        q_.Require(source.exception.has_value(),CloseEntryError::kCatch,edge.source);
        CompletionPayload thrown=CompletionExceptionValue{edge.source};
        if(const auto* explicit_throw=std::get_if<ThrownValue>(&source.exception->exception)) {
          const auto value=Read(edge.source,explicit_throw->value);q_.Require(value.has_value(),CloseEntryError::kPayload,edge.source);thrown=*value;
        }
        CompletionTarget destination=CompletionCaller{};
        const auto& terminal=Node(pending.origin);
        if(terminal.exception && terminal.exception->target) {
          const auto target=terminal.successors[0].target;destination=target;
        }
        close.entries.push_back({{edge,{PendingCompletionKind::kThrow,thrown,destination}},edge.source,catcher,{}});
      }
    } else {
      q_.Reserve(close.entries,pred_offsets_[guard.first.value+1]-pred_offsets_[guard.first.value]);
      for(auto i=pred_offsets_[guard.first.value];i<pred_offsets_[guard.first.value+1];++i) {
        q_.Step();const auto edge=preds_[i];
        q_.Require(Node(edge.source).successors[edge.successor].kind!=ExceptionArcKind::kException &&
            Dominates(success,edge.source),CloseEntryError::kExternalEntry,edge.source);
        close.entries.push_back({{edge,pending.pending},pending.origin,{},pending.transfer});
      }
    }
    q_.Require(!close.entries.empty(),CloseEntryError::kExternalEntry,guard.first);
    Exits(close,region.region);
    std::pmr::vector<CompletionEntry> entries(&q_.memory);
    for(const auto& e:close.entries)entries.push_back(e.entry);
    auto relation=AnalyzeCompletionRelation(body_,profile_,in_.frame.registers,in_.flow,in_.ssa,entries,close.exits,{q_.Remaining()});
    if(const auto* failure=std::get_if<CompletionRelationFailure>(&relation))
      throw CloseEntryFailure{failure->kind==CompletionRelationError::kResourceLimit?CloseEntryError::kResourceLimit:
          failure->kind==CompletionRelationError::kWorkLimit?CloseEntryError::kWorkLimit:CloseEntryError::kCorrespondence,
          region.protocol,region.read.operation,failure->node,*failure};
    close.relation=std::get<CompletionRelation>(std::move(relation));
    q_.memory.Observe(close.relation.peak_bytes);q_.Step(close.relation.work);
    q_.memory.Retain(close.relation.arrivals.capacity()*sizeof(CompletionArrival)+close.relation.leaving.capacity()*sizeof(CompletionLeaving));
    return close;
  }
  const abc::MethodBody& body_;abc::ProfileId profile_;const ValueProgram& g_;
  const PhasedValueInput& in_;const ExceptionPlan& regions_;const IteratorKeys& keys_;const IteratorProtocolScan& scan_;
  mutable Quota q_;ValueIdentities identities_;DominatorTree dom_;LoopForest loops_;
  std::pmr::vector<std::optional<ExceptionNodeId>> before_,commit_,catch_;
  std::pmr::vector<std::optional<SsaValueId>> copy_;
  std::pmr::vector<Dependency> deps_;std::pmr::vector<std::uint8_t> ready_;
  std::pmr::vector<std::optional<ControlNodeId>> normal_;std::pmr::vector<ExceptionNodeId> original_;
  std::pmr::vector<std::optional<Guard>> guards_;std::pmr::vector<std::optional<LoopId>> protocol_loop_;
  std::pmr::vector<std::optional<ExceptionNodeId>> protocol_advance_;
  std::pmr::vector<std::uint32_t> pred_offsets_;std::pmr::vector<CompletionEdge> preds_;
  std::pmr::vector<std::uint32_t> stamps_,loop_begin_,loop_end_;
  std::pmr::vector<std::optional<LoopId>> header_loop_;
  std::pmr::vector<std::optional<CompletionEdge>> exhausted_;
  std::pmr::vector<std::optional<ExceptionNodeId>> success_;
  std::pmr::vector<std::optional<ExceptionNodeId>> value_read_;
  std::pmr::vector<bool> value_ambiguous_;
  std::pmr::vector<CallFact> calls_;
  std::pmr::vector<bool> state_checked_;
  std::pmr::vector<std::uint32_t> value_stamps_;
  std::pmr::vector<std::optional<IteratorProtocolId>> guard_owner_;
  std::pmr::vector<std::uint32_t> region_offsets_,region_index_;
  std::pmr::vector<AliasRange> branch_aliases_;
  std::pmr::vector<abc::RegisterId> alias_slots_;
  std::pmr::vector<std::optional<ExceptionNodeId>> boundaries_;
  std::pmr::vector<std::optional<SsaValueId>> writes_;
  std::pmr::vector<std::uint32_t> read_offsets_,phi_offsets_;
  std::pmr::vector<SsaValueId> phi_values_;
  bool verify_;
  std::uint32_t generation_=0;
};
bool Same(const CompletionEntry& a,const CompletionEntry& b) {
  return a.edge==b.edge && a.pending==b.pending && a.binding==b.binding;
}
std::variant<CloseEntries,CloseEntryFailure> Build(const abc::MethodBody& body,
    abc::ProfileId profile,const ValueProgram& graph,const PhasedValueInput& input,
    const ExceptionPlan& regions,const IteratorKeys& keys,const IteratorProtocolScan& scan,SemanticBudget budget,bool reverse) {
  try {
    Derivation proof(body,profile,graph,input,regions,keys,scan,budget,reverse);
    try{return proof.Run(reverse);}catch(CloseEntryFailure f){if(!f.protocol)f.protocol=proof.current_protocol_;if(!f.read)f.read=proof.current_read_;return f;}
  }catch(CloseEntryFailure f){return f;}
  catch(const std::bad_alloc&){return CloseEntryFailure{CloseEntryError::kResourceLimit};}
  catch(const std::length_error&){return CloseEntryFailure{CloseEntryError::kResourceLimit};}
}
}
std::variant<CloseEntries,CloseEntryFailure> DeriveCloseEntries(const abc::MethodBody& body,
    abc::ProfileId profile,const ValueProgram& graph,const PhasedValueInput& input,
    const ExceptionPlan& regions,const IteratorKeys& keys,const IteratorProtocolScan& scan,SemanticBudget budget) {
  return Build(body,profile,graph,input,regions,keys,scan,budget,false);
}
std::variant<std::uint64_t,CloseEntryFailure> VerifyCloseEntries(const abc::MethodBody& body,
    abc::ProfileId profile,const ValueProgram& graph,const PhasedValueInput& input,
    const ExceptionPlan& regions,const IteratorKeys& keys,const IteratorProtocolScan& scan,
    const CloseEntries& supplied,SemanticBudget budget) {
  const auto rebuilt=Build(body,profile,graph,input,regions,keys,scan,budget,true);
  if(const auto* f=std::get_if<CloseEntryFailure>(&rebuilt))return *f;
  const auto& expected=std::get<CloseEntries>(rebuilt);
  auto work=expected.work;
  const auto step=[&]() {if(work==budget.work)return false;++work;return true;};
  if(expected.closes.size()!=supplied.closes.size())return CloseEntryFailure{CloseEntryError::kCorrespondence};
  for(std::size_t i=0;i<expected.closes.size();++i) {
    const auto& a=expected.closes[i];const auto& b=supplied.closes[i];
    if(!step())return CloseEntryFailure{CloseEntryError::kWorkLimit};
    if(a.protocol!=b.protocol || a.read!=b.read || a.iteration!=b.iteration || a.predicate!=b.predicate ||
        a.state_write!=b.state_write || a.precision!=b.precision || a.entries.size()!=b.entries.size() || a.exits.size()!=b.exits.size() ||
        a.relation.arrivals.size()!=b.relation.arrivals.size() || a.relation.leaving.size()!=b.relation.leaving.size())
      return CloseEntryFailure{CloseEntryError::kCorrespondence,a.protocol,a.read};
    for(std::size_t e=0;e<a.exits.size();++e) {
      if(!step())return CloseEntryFailure{CloseEntryError::kWorkLimit};
      if(a.exits[e]!=b.exits[e])return CloseEntryFailure{CloseEntryError::kCorrespondence,a.protocol,a.read};
    }
    for(std::size_t e=0;e<a.relation.arrivals.size();++e) {
      if(!step())return CloseEntryFailure{CloseEntryError::kWorkLimit};
      if(a.relation.arrivals[e]!=b.relation.arrivals[e])return CloseEntryFailure{CloseEntryError::kCorrespondence,a.protocol,a.read};
    }
    for(std::size_t e=0;e<a.relation.leaving.size();++e) {
      if(!step())return CloseEntryFailure{CloseEntryError::kWorkLimit};
      if(a.relation.leaving[e]!=b.relation.leaving[e])return CloseEntryFailure{CloseEntryError::kCorrespondence,a.protocol,a.read};
    }
    for(std::size_t e=0;e<a.entries.size();++e) {
      const auto& x=a.entries[e];const auto& y=b.entries[e];
      if(!step())return CloseEntryFailure{CloseEntryError::kWorkLimit};
      if(!Same(x.entry,y.entry)||x.origin!=y.origin || x.catcher!=y.catcher || x.transfer!=y.transfer)
        return CloseEntryFailure{CloseEntryError::kCorrespondence,a.protocol,a.read};
    }
  }
  return work;
}
}  // namespace abcd::source
