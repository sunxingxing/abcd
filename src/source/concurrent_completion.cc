#include "concurrent_completion.h"
#include "../analysis/value_phi.h"
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
namespace abcd::source {
namespace {
using namespace analysis;
constexpr std::size_t kFactChanges = 3; // unreachable -> path fact -> conflict.
class Proof {
 public:
  Proof(const ValueProgram& g, SemanticBudget b) : g_(g), initial_(b), left_(b) {}
  ConcurrentCompletion Run() {
    Require(!g_.blocks.empty() && g_.ops.size() <= UINT32_MAX,ConcurrentError::kCorrespondence);
    auto facts=AnalyzeValueIdentities(g_,left_);
    if(const auto* error=std::get_if<ValueSimplifyError>(&facts))
      Fail(*error==ValueSimplifyError::kResourceLimit?ConcurrentError::kResourceLimit:
           *error==ValueSimplifyError::kWorkLimit?ConcurrentError::kWorkLimit:ConcurrentError::kCorrespondence);
    identities_=std::get<ValueIdentities>(std::move(facts));
    Admit(identities_.allocated_bytes,1);Step(identities_.work);
    result_.revision=g_.revision;result_.cfg_revision=g_.cfg_revision;
    Array(result_.members,g_.ops.size());Array(result_.uses,g_.uses.size());
    Array(in_,g_.blocks.size());Array(out_,g_.blocks.size());
    Array(returned_,g_.ops.size());Array(reachable_,g_.blocks.size());
    Require(g_.blocks.size()<=SIZE_MAX/kFactChanges,ConcurrentError::kResourceLimit);
    Array(queue_,g_.blocks.size()*kFactChanges);Array(degree_,g_.blocks.size());
    for(std::uint32_t i=0;i<g_.ops.size();++i) {
      Step();const auto& op=g_.ops[i];if(op.life!=ValueLife::kLive)continue;
      if(const auto* n=std::get_if<ValueConcurrentNotification>(&op.operation)) {
        current_={i};has_current_=true;Require(n->owner==NotificationOwner::kInvocationFunction,ConcurrentError::kNotificationOwner);
        Require(op.use_count==1&&!op.result&&op.first_use.value<g_.uses.size()&&
            op.normal==(LanguageEffect::kConcurrentNotification|LanguageEffect::kUserCode)&&
            op.before_throw==LanguageEffect::kNone&&op.exceptional==ExceptionalCompletion::kImpossible,
            ConcurrentError::kCorrespondence);
        const auto& use=g_.uses[op.first_use.value];
        Require(use.life==ValueLife::kLive&&use.owner==decltype(use.owner){current_}&&
            use.role==OperandRole::kReturnValue&&use.purpose==ReadPurpose::kProtocol,
            ConcurrentError::kCorrespondence);
        result_.uses[op.first_use.value]=1;++count_;
      }
    }
    Admit(count_,sizeof(ConcurrentReturn));result_.returns.reserve(count_);
    Admit(result_.returns.capacity()-count_,sizeof(ConcurrentReturn));
    // Finite flat lattice: unreachable -> one path fact -> conflict. Every
    // block is queued at most three times; no per-notification CFG traversal.
    in_[0]={State::kUnnotified,{}};Push({0});
    while(head_<end_) {
      const auto block=queue_[head_++];Step();reachable_[block.value]=1;
      auto state=Transfer(block,in_[block.value],false);
      if(state==out_[block.value])continue;
      out_[block.value]=state;
      const auto& b=g_.blocks[block.value];
      Require(b.first_edge.value<=g_.edges.size()&&b.edge_count<=g_.edges.size()-b.first_edge.value,ConcurrentError::kCorrespondence);
      for(std::size_t e=b.first_edge.value;e<b.first_edge.value+b.edge_count;++e) {
        Step();const auto& edge=g_.edges[e];Require(edge.source==block&&edge.target.value<in_.size(),ConcurrentError::kCorrespondence);
        auto joined=Join(in_[edge.target.value],state);
        if(joined!=in_[edge.target.value]){in_[edge.target.value]=joined;Push(edge.target);}
      }
    }
    // Only a stable path fact can grant a completion port. Effectful or
    // nonterminating suffixes after notification cannot move to native return.
    for(std::uint32_t b=0;b<g_.blocks.size();++b) {
      Step();if(!reachable_[b])continue;Transfer({b},in_[b],true);
      if(out_[b].state!=State::kNotified)continue;
      const auto& block=g_.blocks[b];
      Require(block.edge_count!=0,ConcurrentError::kCompletion);
      for(std::size_t e=block.first_edge.value;e<block.first_edge.value+block.edge_count;++e){Step();++degree_[g_.edges[e].target.value];}
    }
    head_=end_=0;std::size_t pending=0,visited=0;
    for(std::uint32_t b=0;b<g_.blocks.size();++b) {
      Step();if(reachable_[b]&&in_[b].state==State::kNotified){++pending;if(!degree_[b])Push({b});}
    }
    // Include notify-producing blocks as DAG sources; an entry may also be
    // reached unnotified, so it need not itself have a notified input.
    for(std::uint32_t b=0;b<g_.blocks.size();++b)
      if(reachable_[b]&&out_[b].state==State::kNotified&&in_[b].state!=State::kNotified){Step();Push({b});}
    while(head_<end_) {
      const auto b=queue_[head_++];Step();if(in_[b.value].state==State::kNotified)++visited;
      if(out_[b.value].state!=State::kNotified)continue;
      const auto& block=g_.blocks[b.value];
      for(std::size_t e=block.first_edge.value;e<block.first_edge.value+block.edge_count;++e) {
        Step();const auto target=g_.edges[e].target;Require(degree_[target.value]!=0,ConcurrentError::kCorrespondence);
        if(!--degree_[target.value])Push(target);
      }
    }
    if(visited!=pending){for(std::size_t b=0;b<g_.blocks.size();++b)if(degree_[b]&&in_[b].state==State::kNotified){current_=in_[b].notification;has_current_=true;break;}Fail(ConcurrentError::kCompletion);}
    for(std::uint32_t i=0;i<g_.ops.size();++i) {
      Step();if(g_.ops[i].life!=ValueLife::kLive||!std::holds_alternative<ValueConcurrentNotification>(g_.ops[i].operation))continue;
      current_={i};has_current_=true;Require(returned_[i].has_value(),ConcurrentError::kCompletion);
      const auto port=static_cast<std::uint32_t>(result_.returns.size());
      result_.members[i]=port;result_.returns.push_back({{i},*returned_[i],g_.ops[i].first_use});
    }
    result_.allocated_bytes=initial_.bytes-left_.bytes;result_.work=initial_.work-left_.work;return std::move(result_);
  }
 private:
  enum class State:std::uint8_t{kUnreachable,kUnnotified,kNotified,kConflict};
  struct Fact {State state=State::kUnreachable;OpId notification{};bool operator==(const Fact&)const=default;};
  Fact Join(Fact a,Fact b) {
    if(a==b||b.state==State::kUnreachable)return a;if(a.state==State::kUnreachable)return b;
    return {State::kConflict,{}};
  }
  ValueOperand Canonical(ValueOperand value) {
    if(const auto* id=std::get_if<ValueId>(&value)) {
      Require(id->value<identities_.canonical.size(),ConcurrentError::kCorrespondence);
      const auto root=identities_.canonical[id->value];value=root;
      const auto* result=std::get_if<OpResult>(&g_.values[root.value].origin);
      if(result){const auto& op=g_.ops[result->op.value];if(std::holds_alternative<ValueCopy>(op.operation)&&op.use_count==1){const auto& input=g_.uses[op.first_use.value].operand;if(!std::holds_alternative<ValueId>(input))value=input;}}
    }
    return value;
  }
  bool Same(ValueOperand a,ValueOperand b) {
    a=Canonical(a);b=Canonical(b);if(a.index()!=b.index())return false;
    return std::visit([&](const auto& value){using T=std::decay_t<decltype(value)>;
      if constexpr(std::is_same_v<T,IntegerLiteral>||std::is_same_v<T,Float64Bits>)return value.value==std::get<T>(b).value;
      else return value==std::get<T>(b);
    },a);
  }
  Fact Transfer(WorkBlockId block,Fact state,bool verify) {
    const auto& b=g_.blocks[block.value];
    Require(b.first_op.value<=g_.ops.size()&&b.op_count<=g_.ops.size()-b.first_op.value,ConcurrentError::kCorrespondence);
    for(std::uint32_t j=0;j<b.op_count;++j) {
      Step();current_={b.first_op.value+j};has_current_=true;const auto& op=g_.ops[current_.value];
      if(op.life!=ValueLife::kLive)continue;
      if(std::holds_alternative<ValueConcurrentNotification>(op.operation)) {
        if(verify)Require(state.state==State::kUnnotified, state.state==State::kNotified?ConcurrentError::kDuplicateNotification:ConcurrentError::kCompletion);
        state={State::kNotified,current_};continue;
      }
      if(std::holds_alternative<ValueReturn>(op.operation)) {
        if(verify) {
          Require(state.state==State::kNotified,ConcurrentError::kMissingNotification);
          const auto& notification=g_.ops[state.notification.value];
          Require(op.use_count==1&&op.first_use.value<g_.uses.size(),ConcurrentError::kCorrespondence);
          Require(Same(g_.uses[notification.first_use.value].operand,g_.uses[op.first_use.value].operand),ConcurrentError::kNotificationValue);
          // A native return port has one physical original notification. A
          // path-dependent many-to-one event needs separate port correlation.
          Require(!returned_[state.notification.value]||*returned_[state.notification.value]==current_,ConcurrentError::kCorrespondence);
          returned_[state.notification.value]=current_;
        }
        state={State::kUnnotified,{}};continue;
      }
      if(verify&&state.state==State::kNotified)
        Require(op.normal==LanguageEffect::kNone&&op.before_throw==LanguageEffect::kNone&&op.exceptional==ExceptionalCompletion::kImpossible,ConcurrentError::kEarlyNotification);
    }
    return state;
  }
  [[noreturn]] void Fail(ConcurrentError kind)const {
    ConcurrentFailure e;e.kind=kind;
    if(kind==ConcurrentError::kResourceLimit||kind==ConcurrentError::kWorkLimit||!has_current_)throw e;
    e.operation=current_;
    if(current_.value<g_.ops.size()) {const auto origin=g_.ops[current_.value].origin;if(origin.value<g_.origins.size()){e.pc=g_.origins[origin.value].original.value_or(g_.origins[origin.value].pc);e.working_pc=g_.origins[origin.value].pc;}}
    throw e;
  }
  void Require(bool c,ConcurrentError k)const{if(!c)Fail(k);}
  void Step(std::uint64_t n=1){Require(n<=left_.work,ConcurrentError::kWorkLimit);left_.work-=n;}
  void Admit(std::size_t n,std::size_t width){Require(n<=left_.bytes/width,ConcurrentError::kResourceLimit);left_.bytes-=n*width;}
  template<class T>void Array(std::vector<T>& v,std::size_t n){Admit(n,sizeof(T));Step(n);v.resize(n);Admit(v.capacity()-n,sizeof(T));}
  void Push(WorkBlockId b){Require(end_<queue_.size(),ConcurrentError::kResourceLimit);queue_[end_++]=b;}
  const ValueProgram& g_;SemanticBudget initial_,left_;ValueIdentities identities_;ConcurrentCompletion result_;
  OpId current_{};bool has_current_=false;std::size_t count_=0,head_=0,end_=0;
  std::vector<Fact> in_,out_;std::vector<std::optional<OpId>> returned_;
  std::vector<std::uint8_t> reachable_;std::vector<WorkBlockId> queue_;std::vector<std::uint32_t> degree_;
};
}
std::variant<ConcurrentCompletion,ConcurrentFailure> BuildConcurrentCompletion(const analysis::ValueProgram& g,analysis::SemanticBudget budget) {
  try{return Proof(g,budget).Run();}catch(const ConcurrentFailure& e){return e;}
  catch(const std::bad_alloc&){ConcurrentFailure e;e.kind=ConcurrentError::kResourceLimit;return e;}
  catch(const std::length_error&){ConcurrentFailure e;e.kind=ConcurrentError::kResourceLimit;return e;}
}
std::variant<std::uint64_t,ConcurrentFailure> VerifyConcurrentCompletion(const analysis::ValueProgram& g,const ConcurrentCompletion& proof,analysis::SemanticBudget budget) {
  auto checked=BuildConcurrentCompletion(g,budget);if(const auto* e=std::get_if<ConcurrentFailure>(&checked))return *e;
  const auto& actual=std::get<ConcurrentCompletion>(checked);
  if(actual.revision!=proof.revision||actual.cfg_revision!=proof.cfg_revision||actual.returns!=proof.returns||actual.members!=proof.members||actual.uses!=proof.uses){ConcurrentFailure e;e.kind=ConcurrentError::kCorrespondence;return e;}
  return actual.work;
}
}
