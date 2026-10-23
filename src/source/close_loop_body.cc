#include "close_loop_body.h"
#include "../analysis/idf_scratch.h"
#include "abcd/analysis/dominators.h"
#include <algorithm>
#include <bit>
#include <set>
#include <tuple>

namespace abcd::source {
namespace {
using namespace analysis;
struct NodeLess {
  std::uint64_t* left;
  bool operator()(ExceptionNodeId a,ExceptionNodeId b)const {
    if(!*left)throw CloseCompletionFailure{CloseCompletionError::kWorkLimit};--*left;return a<b;
  }
};
class Body {
 public:
  Body(const ExceptionFlow& flow,const ExceptionPlan& plan,const DerivedClose& close,TryId region,
      SemanticBudget budget,bool reverse,const CloseBodyIndex* shared):flow_(flow),plan_(plan),c_(close),region_(region),memory_(budget.bytes),
      left_(budget.work),initial_(budget.work),owned_before_(&memory_),owned_commit_(&memory_),
      body_(NodeLess{&left_},&memory_),prefix_(NodeLess{&left_},&memory_),suffix_(NodeLess{&left_},&memory_),reverse_(reverse) {
    Require(region.value<plan.layout.regions.size());
    if(shared) {
      Require(shared->flow==&flow && shared->before.size()==flow.instructions.size() && shared->commit.size()==flow.instructions.size());
      before_=shared->before;commit_=shared->commit;ports_=shared->ports;members_=shared->members;
    } else {
      owned_before_.resize(flow.instructions.size());owned_commit_.resize(flow.instructions.size());
      for(std::uint32_t n=0;n<flow.nodes.size();++n) {
        Step();const auto& node=flow.nodes[n];
        if(node.phase==ExceptionPhase::kBefore)owned_before_[node.instruction.value]=ExceptionNodeId{n};
        if(node.phase==ExceptionPhase::kCommit)owned_commit_[node.instruction.value]=ExceptionNodeId{n};
      }
      before_=owned_before_;commit_=owned_commit_;
    }
    Dominance(shared);
  }
  CloseProtectedRange Run() {
    CloseProtectedRange out{region_,ProtectedRangeRole::kLoopBody};out_=&out;
    if(!c_.iteration.value || c_.iteration.value->value>=flow_.nodes.size() ||
        Node(*c_.iteration.value).phase!=ExceptionPhase::kBefore || !Dominates(c_.iteration.success,*c_.iteration.value)) {
      Missing(CloseOwnershipGap::kBodyIdentity,c_.iteration.value);return Finish(std::move(out));
    }
    const auto value=*c_.iteration.value;
    const auto value_commit=commit_[Node(value).instruction.value];
    if(!value_commit || Node(*value_commit).successor_count!=1) {
      Missing(CloseOwnershipGap::kBodyIdentity,value);return Finish(std::move(out));
    }
    // The typed key/SSA proof in CloseEntries binds this Value read to the
    // same next-result. Here original dominance/edges define the body region.
    const auto entry=Node(*value_commit).successors[0].target;
    std::pmr::vector<ExceptionNodeId> todo(&memory_);todo.push_back(entry);body_.insert(entry);
    std::pmr::vector<CompletionEdge> dispatch(&memory_),exits(&memory_);
    for(std::size_t i=0;i<todo.size();++i) {
      Step();const auto id=todo[i];const auto& node=Node(id);
      if(!Dominates(*value_commit,id) || !Dominates(c_.iteration.success,id))Missing(CloseOwnershipGap::kBodyIdentity,id);
      if(node.phase==ExceptionPhase::kBefore && std::holds_alternative<ReturnOperation>(Instruction(id).operation))
        Missing(CloseOwnershipGap::kBodyExit,id);
      for(std::uint8_t j=0;j<node.successor_count;++j) {
        Step();const auto e=static_cast<std::uint8_t>(reverse_?node.successor_count-j-1:j);
        const auto& arc=node.successors[e];const CompletionEdge edge{id,e};
        if(arc.kind==ExceptionArcKind::kException) {
          if(ThrowEntry(edge))dispatch.push_back(edge);
          else if(Nested(arc.target)){if(body_.insert(arc.target).second)todo.push_back(arc.target);}
          else Missing(CloseOwnershipGap::kBodyDispatch,id,edge);
          continue;
        }
        if(Port(edge)) {exits.push_back(edge);continue;}
        if(c_.iteration.loop_header && arc.target==*c_.iteration.loop_header){exits.push_back(edge);continue;}
        if(arc.target==c_.iteration.exit || arc.target==c_.iteration.success || arc.target==value ||
            arc.target==c_.iteration.advance || (node.phase==ExceptionPhase::kCommit && std::holds_alternative<ReturnOperation>(Instruction(id).operation))) {
          Missing(CloseOwnershipGap::kBodyExit,id,edge);continue;
        }
        if(Node(arc.target).phase==ExceptionPhase::kCatch) {Missing(CloseOwnershipGap::kBodyExit,id,edge);continue;}
        if(body_.insert(arc.target).second)todo.push_back(arc.target);
      }
    }
    // Boundary transport is proved by semantics, never by opcode adjacency or
    // PC intervals. It stays ordinary original control; no copies are elided.
    auto n=entry;
    while(body_.contains(n) && Transparent(n) && !prefix_.contains(n)) {
      Step();prefix_.insert(n);n=Node(n).successors[0].target;
    }
    Suffix(todo,exits);
    const auto& range=plan_.layout.regions[region_.value].body;
    Require(range.begin.value<=range.end.value && range.end.value<before_.size()+1);
    // Every body op beyond entry/exit transports must be protected; every
    // live protected op must be in this independently derived body.
    for(const auto id:todo)if(Node(id).phase==ExceptionPhase::kBefore) {
      Step();const auto i=Node(id).instruction.value;
      if(i<range.begin.value || i>=range.end.value)
        if(!prefix_.contains(id) && !suffix_.contains(id))Missing(CloseOwnershipGap::kBodyRange,id);
    }
    for(auto i=range.begin.value;i<range.end.value;++i) {
      Step();Require(before_[i].has_value());const auto id=*before_[i];
      if(dom_->nodes[id.value].begin!=0 && Member(id)) {
        // This is a different verified close copy reached by an abrupt body
        // port. Its operations/dispatch remain in that close certificate.
        continue;
      }
      if(dom_->nodes[id.value].begin!=0 && !body_.contains(id))Missing(CloseOwnershipGap::kBodyRange,id);
      if(dom_->nodes[id.value].begin!=0 && Node(id).exception) {
        bool found=false;
        for(std::uint8_t e=0;e<Node(id).successor_count;++e)if(Node(id).successors[e].kind==ExceptionArcKind::kException) {
          Step();const CompletionEdge edge{id,e};const bool accepted=ThrowEntry(edge) || Nested(Node(id).successors[e].target);found|=accepted;
          if(!accepted)Missing(CloseOwnershipGap::kBodyDispatch,id,edge);
        }
        if(!found)Missing(CloseOwnershipGap::kBodyDispatch,id);
      }
    }
    Sort(todo);Sort(dispatch);Sort(exits);
    Reserve(out.ordinary,todo.size());Reserve(out.transport,todo.size());
    for(const auto id:todo){Step();out.ordinary.push_back(id);if(prefix_.contains(id) || suffix_.contains(id))out.transport.push_back(id);}
    auto begin=std::lower_bound(members_.begin(),members_.end(),CloseBodyMember{c_.protocol,{}},
        [&](const auto& a,const auto& b){Step();return a<b;});
    for(auto it=begin;it!=members_.end() && it->protocol==c_.protocol;++it) {
      Step();const auto instruction=Node(it->node).instruction.value;
      if(range.begin.value<=instruction && instruction<range.end.value && !body_.contains(it->node)) {
        if(out.protocol.size()==out.protocol.capacity()) {
          const auto capacity=out.protocol.empty()?1:out.protocol.size()*2;
          Reserve(out.protocol,capacity);
        }
        out.protocol.push_back(it->node);
      }
    }
    Reserve(out.dispatch,dispatch.size());out.dispatch.assign(dispatch.begin(),dispatch.end());
    Reserve(out.exits,exits.size());out.exits.assign(exits.begin(),exits.end());
    return Finish(std::move(out));
  }
 private:
  void Step(std::uint64_t n=1){if(n>left_)throw CloseCompletionFailure{CloseCompletionError::kWorkLimit};left_-=n;}
  void Require(bool v){if(!v)throw CloseCompletionFailure{CloseCompletionError::kCorrespondence,c_.protocol,c_.read};}
  const ExceptionNode& Node(ExceptionNodeId n)const{return flow_.nodes[n.value];}
  const SemanticInstruction& Instruction(ExceptionNodeId n)const{return flow_.instructions[Node(n).instruction.value];}
  bool Dominates(ExceptionNodeId a,ExceptionNodeId b){Step();return a.value<dom_->nodes.size() && b.value<dom_->nodes.size() && dom_->Dominates({a.value},{b.value});}
  void Missing(CloseOwnershipGap gap,std::optional<ExceptionNodeId> node={},std::optional<CompletionEdge> edge={}) {
    const CloseCompletionFailure next{CloseCompletionError::kOwnership,c_.protocol,c_.read,{},node,edge,{}, {}, {},gap,region_};
    const auto key=[](const CloseCompletionFailure& f){return std::tuple{f.ownership_gap!=CloseOwnershipGap::kBodyIdentity,f.ownership_gap!=CloseOwnershipGap::kBodyDispatch,
        f.ownership_gap!=CloseOwnershipGap::kBodyExit,f.ownership_gap,f.node,f.edge};};
    if(!out_->missing || key(next)<key(*out_->missing))out_->missing=next;
  }
  CloseProtectedRange Finish(CloseProtectedRange out){out.peak_bytes=memory_.Peak();out.work=initial_-left_;return out;}
  template<class T>void Reserve(std::vector<T>& out,std::size_t count){if(count>memory_.Available()/sizeof(T))throw std::bad_alloc();memory_.Retain(count*sizeof(T));out.reserve(count);memory_.Retain((out.capacity()-count)*sizeof(T));}
  template<class T>void Sort(std::pmr::vector<T>& v){std::sort(v.begin(),v.end(),[&](const T&a,const T&b){Step();return a<b;});}
  bool Transparent(ExceptionNodeId id) {
    Step();const auto& node=Node(id);const auto& op=Instruction(id);
    if(node.successor_count!=1 || node.successors[0].kind==ExceptionArcKind::kException || node.phase==ExceptionPhase::kCatch ||
        op.effects.exceptional!=ExceptionalCompletion::kImpossible || op.effects.normal!=LanguageEffect::kNone || op.effects.before_throw!=LanguageEffect::kNone ||
        std::holds_alternative<ReturnOperation>(op.operation) || std::holds_alternative<ThrowOperation>(op.operation))return false;
    return true;
  }
  bool Port(CompletionEdge edge) {
    Step(1+std::bit_width(ports_.size()));
    return std::binary_search(ports_.begin(),ports_.end(),CloseBodyPort{c_.protocol,edge});
  }
  bool Member(ExceptionNodeId n) {
    Step(1+std::bit_width(members_.size()));
    return std::binary_search(members_.begin(),members_.end(),CloseBodyMember{c_.protocol,n});
  }
  bool ThrowEntry(CompletionEdge edge) {
    // Sorted once; no entry-count x body-size scan. The Catch identity must
    // be THIS wrapper's handler and the pending Throw belongs to this source.
    Step(1+std::bit_width(entries_.size()));return std::binary_search(entries_.begin(),entries_.end(),edge);
  }
  bool Nested(ExceptionNodeId target) {
    // A try wholly inside this wrapper's protected body (body and handler) is
    // ordinary loop-body control: its Catch is internal flow. Whatever escapes
    // its handler must still reach this wrapper through a proved Throw entry.
    Step(1+std::bit_width(nested_.size()));const auto& node=Node(target);
    return node.phase==ExceptionPhase::kCatch && std::binary_search(nested_.begin(),nested_.end(),node.instruction);
  }
  void Dominance(const CloseBodyIndex* shared) {
    if(shared) {dom_=shared->dominance;Require(dom_ && dom_->nodes.size()==flow_.nodes.size());}
    else {
    std::pmr::vector<std::uint32_t> offsets(&memory_);std::pmr::vector<BlockId> edges(&memory_);
    offsets.push_back(0);
    for(const auto& node:flow_.nodes){Step();for(std::uint8_t e=0;e<node.successor_count;++e){Step();edges.push_back({node.successors[e].target.value});}offsets.push_back(static_cast<std::uint32_t>(edges.size()));}
    auto dom=BuildDominators({offsets,edges},{0},{memory_.Available(),left_});
    if(const auto* f=std::get_if<DominatorError>(&dom))throw CloseCompletionFailure{*f==DominatorError::kWorkLimit?CloseCompletionError::kWorkLimit:*f==DominatorError::kResourceLimit?CloseCompletionError::kResourceLimit:CloseCompletionError::kCorrespondence};
    owned_dom_=std::get<DominatorTree>(std::move(dom));dom_=&owned_dom_;
    memory_.Observe(dom_->work.allocated_bytes);memory_.Retain(dom_->work.allocated_bytes);
    Step(dom_->work.steps+dom_->work.eval_steps+dom_->work.edge_visits);
    }
    for(const auto& e:c_.entries) {
      Step();if(e.catcher && e.entry.pending.kind==PendingCompletionKind::kThrow && Node(*e.catcher).instruction==plan_.layout.regions[region_.value].handler.begin)
        entries_.push_back(e.entry.edge);
    }
    Sort(entries_);
    const auto& wrapper=plan_.layout.regions[region_.value].body;
    for(std::uint32_t t=0;t<plan_.layout.regions.size();++t) {
      Step();if(t==region_.value)continue;const auto& r=plan_.layout.regions[t];
      if(wrapper.begin<=r.body.begin && r.body.end<=wrapper.end && wrapper.begin<=r.handler.begin && r.handler.end<=wrapper.end)nested_.push_back(r.handler.begin);
    }
    Sort(nested_);
  }
  void Suffix(const std::pmr::vector<ExceptionNodeId>& members,const std::pmr::vector<CompletionEdge>& exits) {
    std::pmr::vector<std::pair<ExceptionNodeId,ExceptionNodeId>> pred(&memory_);
    for(const auto n:members){Step();if(Transparent(n))pred.push_back({Node(n).successors[0].target,n});}
    Sort(pred);
    std::pmr::vector<ExceptionNodeId> todo(&memory_);
    for(const auto edge:exits){Step();if(Transparent(edge.source) && suffix_.insert(edge.source).second)todo.push_back(edge.source);}
    for(std::size_t n=0;n<todo.size();++n) {
      auto first=std::lower_bound(pred.begin(),pred.end(),todo[n],[&](const auto& p,ExceptionNodeId id){Step();return p.first<id;});
      for(auto i=first;i!=pred.end() && i->first==todo[n];++i){Step();if(suffix_.insert(i->second).second)todo.push_back(i->second);}
    }
  }
  const ExceptionFlow& flow_;const ExceptionPlan& plan_;const DerivedClose& c_;TryId region_;
  detail::IdfScratch memory_;std::uint64_t left_,initial_;
  std::pmr::vector<std::optional<ExceptionNodeId>> owned_before_,owned_commit_;
  std::span<const std::optional<ExceptionNodeId>> before_,commit_;
  std::span<const CloseBodyPort> ports_;
  std::span<const CloseBodyMember> members_;
  std::pmr::set<ExceptionNodeId,NodeLess> body_,prefix_,suffix_;
  std::pmr::vector<CompletionEdge> entries_{&memory_};
  std::pmr::vector<InstructionId> nested_{&memory_};
  DominatorTree owned_dom_;const DominatorTree* dom_=nullptr;bool reverse_;CloseProtectedRange* out_=nullptr;
};
}
std::variant<CloseProtectedRange,CloseCompletionFailure> ProveCloseLoopBody(const ExceptionFlow& flow,
    const ExceptionPlan& plan,const DerivedClose& close,TryId region,SemanticBudget budget,bool reverse,const CloseBodyIndex* shared) {
  try{return Body(flow,plan,close,region,budget,reverse,shared).Run();}
  catch(const CloseCompletionFailure& f){return f;}
  catch(const std::bad_alloc&){return CloseCompletionFailure{CloseCompletionError::kResourceLimit,close.protocol,close.read};}
  catch(const std::length_error&){return CloseCompletionFailure{CloseCompletionError::kResourceLimit,close.protocol,close.read};}
}
}
