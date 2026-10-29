#include "cell_parameter_default.h"
#include "../analysis/value_flow.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
namespace abcd::source {
namespace {
using namespace analysis;
using Error = CellParameterDefaultError;
class Analysis {
 public:
  Analysis(const ValueProgram& g, const ValueEnvironments& e, const CellPlan& c,
      const ParameterPlan& p, SemanticBudget budget)
      : g_(g), e_(e), c_(c), p_(p), initial_(budget), left_(budget) {}
  CellParameterDefaultCost Cost() const { return {initial_.bytes-left_.bytes,initial_.work-left_.work}; }
  void Step(std::uint64_t n=1) { Require(n<=left_.work,Error::kWorkLimit); left_.work-=n; }
  void Charge(std::size_t n,std::size_t w) { Require(n<=left_.bytes/w,Error::kResourceLimit);left_.bytes-=n*w; }
  template<class T> void Size(std::vector<T>& v,std::size_t n) {
    Charge(n,sizeof(T));Step(n);v.resize(n);Charge(v.capacity()-n,sizeof(T));
  }
  template<class T> void Reserve(std::vector<T>& v,std::size_t n) {
    Charge(n,sizeof(T));v.reserve(n);Charge(v.capacity()-n,sizeof(T));
  }
  void Require(bool ok,Error error) const { if(!ok){auto f=failure_;f.kind=error;throw f;} }
  void At(OpId id) { failure_.location=OperationLocation(g_,id); }
  void Context(const CellParameterDefault& d) {
    failure_={};failure_.parameter=d.parameter;failure_.local=d.destination;failure_.outer=d.source;At(d.branch);
  }
  void Index() {
    Require(g_.phases.empty(),Error::kCatchState);
    Require(p_.revision==g_.revision&&p_.cfg_revision==g_.cfg_revision,Error::kCorrespondence);
    const auto cells=VerifyCellPlan(g_,e_,c_,left_);
    if(const auto* error=std::get_if<CellError>(&cells))
      Require(false,*error==CellError::kResourceLimit?Error::kResourceLimit:*error==CellError::kWorkLimit?Error::kWorkLimit:Error::kCorrespondence);
    Step(std::get<std::uint64_t>(cells));
    auto flow=VerifyValueFlow(g_,left_);
    if(const auto* error=std::get_if<ValueAvailabilityError>(&flow))
      Require(false,*error==ValueAvailabilityError::kResourceLimit?Error::kResourceLimit:*error==ValueAvailabilityError::kWorkLimit?Error::kWorkLimit:Error::kCorrespondence);
    auto& f=std::get<ValueFlowProof>(flow);Charge(f.allocated_bytes,1);Step(f.work);dom_=std::move(f.dominators);
    Size(canonical_,g_.values.size());Size(colors_,g_.values.size());Reserve(path_,g_.values.size());
    // Copy identity is computed once even for reverse-numbered definitions.
    for(std::uint32_t i=0;i<g_.values.size();++i) {
      Step();if(colors_[i]==2)continue;ValueId v{i};path_.clear();ValueOperand end=v;
      for(;;) {
        Step();Require(v.value<g_.values.size(),Error::kCorrespondence);
        if(colors_[v.value]==2){end=canonical_[v.value];break;}
        Require(colors_[v.value]!=1,Error::kCorrespondence);colors_[v.value]=1;path_.push_back(v);
        const auto* def=std::get_if<OpResult>(&g_.values[v.value].origin);
        if(!def||!std::holds_alternative<ValueCopy>(g_.ops[def->op.value].operation)||g_.ops[def->op.value].life!=ValueLife::kLive){end=v;break;}
        const auto& op=g_.ops[def->op.value];Require(op.use_count==1&&op.result==v&&Pure(op),Error::kCorrespondence);
        end=g_.uses[op.first_use.value].operand;
        if(const auto* next=std::get_if<ValueId>(&end))v=*next;else break;
      }
      for(const auto x:path_){Step();canonical_[x.value]=end;colors_[x.value]=2;}
    }
    Size(guards_,g_.values.size());Size(guard_counts_,g_.values.size());Size(exit_,g_.ops.size());Size(role_,g_.ops.size());
    Size(region_,g_.blocks.size());Size(prefix_,g_.blocks.size());Size(walk_color_,g_.blocks.size());
    Reserve(stack_,g_.blocks.size());Size(compare_branch_,g_.ops.size());Size(load_compare_,g_.values.size());
    for(std::uint32_t i=0;i<g_.exceptional_exits.size();++i) {
      Step();const auto& x=g_.exceptional_exits[i];At(x.op);Require(!exit_[x.op.value],Error::kCatchState);
      exit_[x.op.value]=i;
    }
    for(std::uint32_t i=0;i<g_.ops.size();++i) {
      Step();const auto& op=g_.ops[i];if(op.life!=ValueLife::kLive)continue;At({i});
      if(std::holds_alternative<ValueGuard>(op.operation)) {
        const auto value=Operand({i},OperandRole::kGuardValue);
        if(const auto* id=std::get_if<ValueId>(&value)) {
          ++guard_counts_[id->value];if(!guards_[id->value])guards_[id->value]=OpId{i};
        }
      }
      if(const auto* binary=std::get_if<ValueBinary>(&op.operation); binary && (binary->op==BinaryOperator::kStrictEqual || binary->op==BinaryOperator::kStrictNotEqual)) {
        const auto a=Operand({i},OperandRole::kLeft),b=Operand({i},OperandRole::kRight);
        const auto value=Same(a,SpecialValue::kUndefined)?b:Same(b,SpecialValue::kUndefined)?a:ValueOperand{SpecialValue::kUndefined};
        if(const auto* id=std::get_if<ValueId>(&value)) { if(!load_compare_[id->value])load_compare_[id->value]=OpId{i}; }
      }
      if(std::holds_alternative<ValueBranch>(op.operation)) {
        const auto producer=Producer(Operand({i},OperandRole::kCondition));
        if(producer&&!compare_branch_[producer->value])compare_branch_[producer->value]=OpId{i};
      }
    }
    // Real normal postdominators, including a distinct virtual normal exit.
    const auto n=g_.blocks.size();Require(n<UINT32_MAX,Error::kResourceLimit);
    std::vector<std::uint32_t> offsets,cursor;std::vector<BlockId> successors;
    Size(offsets,n+2);Size(cursor,n+1);
    std::size_t exits=0;
    for(const auto& b:g_.blocks){Step();exits+=b.edge_count==0;}
    Size(successors,g_.edges.size()+exits);
    for(const auto& edge:g_.edges){Step();Require(edge.kind!=FlowEdgeKind::kException,Error::kCatchState);++offsets[edge.target.value+1];}
    offsets[n+1]=static_cast<std::uint32_t>(exits);
    for(std::size_t i=1;i<offsets.size();++i){Step();offsets[i]+=offsets[i-1];}
    std::copy(offsets.begin(),offsets.end()-1,cursor.begin());
    for(const auto& edge:g_.edges){Step();successors[cursor[edge.target.value]++]={edge.source.value};}
    for(std::uint32_t i=0;i<n;++i)if(g_.blocks[i].edge_count==0){Step();successors[cursor[n]++]={i};}
    auto post=BuildDominators({offsets,successors},{static_cast<std::uint32_t>(n)},{left_.bytes,left_.work});
    if(const auto* error=std::get_if<DominatorError>(&post))Require(false,*error==DominatorError::kResourceLimit?Error::kResourceLimit:*error==DominatorError::kWorkLimit?Error::kWorkLimit:Error::kCorrespondence);
    post_=std::get<DominatorTree>(std::move(post));Charge(post_.work.allocated_bytes,1);Step(post_.work.steps);
    failure_={};
  }
  std::vector<CellParameterDefault> Discover(std::uint32_t length) {
    std::vector<CellParameterDefault> out;Reserve(out,p_.formals.size());
    // Visit each cell access list once. No per-parameter method walk.
    for(std::uint32_t i=0;i<c_.cells.size();++i) {
      Step();const auto& cell=c_.cells[i];const auto* local=std::get_if<AllocatedCell>(&cell.storage);
      if(!local||cell.count<2)continue;
      const auto& first=c_.accesses[cell.first];if(first.kind!=CellAccessKind::kStore)continue;
      auto value=Operand(first.operation,OperandRole::kStoredValue);const auto* id=std::get_if<ValueId>(&value);
      if(!id)continue;const auto* def=std::get_if<EntryDefinition>(&g_.values[id->value].origin);
      const auto* param=def?std::get_if<ParameterEntry>(def):nullptr;if(!param||param->index<length)continue;
      CellParameterDefault d;d.parameter=param->index;d.entry=*id;d.local={i};d.destination={local->allocation,local->slot};d.initial_store=first.operation;
      const auto& second=c_.accesses[cell.first+1];d.test=second.operation;failure_={};failure_.parameter=d.parameter;failure_.local=d.destination;At(d.test);
      Require(second.kind==CellAccessKind::kLoad&&g_.ops[d.test.value].result.has_value(),Error::kLocalCell);
      const auto comparison=load_compare_[g_.ops[d.test.value].result->value];
      Require(comparison.has_value(),Error::kParameterIdentity);
      d.comparison=*comparison;Require(compare_branch_[comparison->value].has_value(),Error::kParameterIdentity);d.branch=*compare_branch_[comparison->value];At(d.branch);
      const auto join=post_.nodes[g_.ops[d.branch.value].block.value].parent;
      Require(join&&join->value<g_.blocks.size(),Error::kBodyState);d.join={join->value};
      const auto& branch=g_.ops[d.branch.value];const auto& block=g_.blocks[branch.block.value];Require(block.edge_count==2,Error::kParameterIdentity);
      const auto* binary=std::get_if<ValueBinary>(&g_.ops[d.comparison.value].operation);
      const auto* predicate=std::get_if<ValueBranch>(&branch.operation);
      Require(binary&&predicate,Error::kParameterIdentity);
      const bool positive=predicate->predicate==UnaryOperator::kTrueOrNonzeroNumber;
      Require(positive||predicate->predicate==UnaryOperator::kFalseOrZero,Error::kParameterIdentity);
      const bool missing_taken=(binary->op==BinaryOperator::kStrictEqual)==positive;
      for(std::uint32_t j=0;j<block.edge_count;++j){const EdgeId edge{block.first_edge.value+j};
        Require(g_.edges[edge.value].kind==FlowEdgeKind::kTaken||g_.edges[edge.value].kind==FlowEdgeKind::kFallthrough,Error::kParameterIdentity);
        ((g_.edges[edge.value].kind==FlowEdgeKind::kTaken)==missing_taken?d.missing:d.provided)=edge;}
      std::optional<OpId> fallback;
      for(std::uint32_t j=2;j<cell.count;++j){Step();const auto a=c_.accesses[cell.first+j];
        if(a.kind==CellAccessKind::kStore&&!dom_.Dominates({d.join.value},{g_.ops[a.operation.value].block.value})) {
          Require(!fallback,Error::kBodyState);fallback=a.operation;
        }}
      Require(fallback.has_value(),Error::kBodyState);d.fallback_store=*fallback;
      const auto read=Producer(Operand(*fallback,OperandRole::kStoredValue));Require(read.has_value(),Error::kOuterBinding);d.read=*read;
      Require(c_.operations[d.read.value].has_value(),Error::kOuterBinding);d.outer=*c_.operations[d.read.value];
      const auto* incoming=std::get_if<IncomingCell>(&c_.cells[d.outer.value].storage);Require(incoming,Error::kOuterBinding);d.source={incoming->depth,incoming->slot};failure_.outer=d.source;
      Require(g_.ops[d.read.value].result&&guards_[g_.ops[d.read.value].result->value],Error::kGuard);d.guard=*guards_[g_.ops[d.read.value].result->value];
      Require(out.size()<out.capacity(),Error::kParameterIdentity);out.push_back(d);
    }
    Step(out.size()*std::bit_width(out.size()+1));std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.parameter<b.parameter;});
    Require(!out.empty(),Error::kParameterIdentity);return out;
  }
  void Verify(std::span<const CellParameterDefault> defaults,std::uint32_t length) {
    Require(!defaults.empty()&&defaults.front().parameter==length,Error::kParameterOrder);
    for(std::size_t i=0;i<defaults.size();++i) {
      Step();const auto& d=defaults[i];Context(d);
      Require(d.parameter<p_.formals.size()&&p_.formals[d.parameter].entry==d.entry,Error::kParameterIdentity);
      Require(d.entry.value<g_.values.size(),Error::kParameterIdentity);
      const auto* entry=std::get_if<EntryDefinition>(&g_.values[d.entry.value].origin);
      Require(entry&&std::holds_alternative<ParameterEntry>(*entry)&&std::get<ParameterEntry>(*entry).index==d.parameter,Error::kParameterIdentity);
      Require(d.local.value<c_.cells.size()&&d.outer.value<c_.cells.size(),Error::kCorrespondence);
      Require(d.local!=d.outer,Error::kAlias);
      const auto* local=std::get_if<AllocatedCell>(&c_.cells[d.local.value].storage);
      const auto* outer=std::get_if<IncomingCell>(&c_.cells[d.outer.value].storage);
      Require(local&&local->allocation==d.destination.allocation&&local->slot.value==d.destination.slot.value,Error::kLocalCell);
      Require(outer&&outer->depth==d.source.depth&&outer->slot.value==d.source.slot.value,Error::kOuterBinding);
      if(i)Require(defaults[i-1].parameter<d.parameter&&dom_.Dominates({defaults[i-1].join.value},{Op(d.branch).block.value}),Error::kParameterOrder);
      Require(d.join.value<g_.blocks.size()&&post_.nodes[Op(d.branch).block.value].parent==BlockId{d.join.value}&&
          dom_.Dominates({Op(d.branch).block.value},{d.join.value}),Error::kBodyState);
      const auto& init=Op(d.initial_store);const auto& test=Op(d.test);const auto& read=Op(d.read);const auto& store=Op(d.fallback_store);const auto& guard=Op(d.guard);
      Require(std::holds_alternative<ValueEnvironmentPush>(Op(d.destination.allocation).operation),Error::kLocalCell);
      Require(init.exceptional==ExceptionalCompletion::kImpossible&&init.before_throw==LanguageEffect::kNone&&
          init.normal==(LanguageEffect::kLexicalWrite|LanguageEffect::kEnvironmentRead)&&
          store.exceptional==ExceptionalCompletion::kImpossible&&store.before_throw==LanguageEffect::kNone&&
          store.normal==(LanguageEffect::kLexicalWrite|LanguageEffect::kEnvironmentRead),Error::kBodyState);
      Require(test.exceptional==ExceptionalCompletion::kImpossible&&test.before_throw==LanguageEffect::kNone&&
          test.normal==(LanguageEffect::kLexicalRead|LanguageEffect::kEnvironmentRead),Error::kLocalCell);
      Require(std::holds_alternative<ValueLexicalStore>(init.operation)&&c_.operations[d.initial_store.value]==d.local&&
          Same(Operand(d.initial_store,OperandRole::kStoredValue),d.entry)&&Before(d.destination.allocation,d.initial_store)&&Before(d.initial_store,d.test),Error::kLocalCell);
      Require(std::holds_alternative<ValueLexicalLoad>(test.operation)&&test.result&&c_.operations[d.test.value]==d.local,Error::kLocalCell);
      Require(std::holds_alternative<ValueLexicalLoad>(read.operation)&&read.result&&c_.operations[d.read.value]==d.outer&&
          read.normal==(LanguageEffect::kLexicalRead|LanguageEffect::kEnvironmentRead)&&read.before_throw==LanguageEffect::kNone&&read.exceptional==ExceptionalCompletion::kImpossible,Error::kOuterBinding);
      Require(guard_counts_[read.result->value]==1,Error::kGuard);
      const auto* check=std::get_if<ValueGuard>(&guard.operation);
      Require(check&&check->name&&guard.normal==LanguageEffect::kGuard&&guard.before_throw==LanguageEffect::kGuard&&
          guard.exceptional==ExceptionalCompletion::kMayThrow&&Same(Operand(d.guard,OperandRole::kGuardValue),*read.result)&&Before(d.read,d.guard)&&Before(d.guard,d.fallback_store),Error::kGuard);
      At(d.guard);Require(exit_[d.guard.value].has_value(),Error::kCatchState);
      const auto& exit=g_.exceptional_exits[*exit_[d.guard.value]];
      Require(!exit.handler&&exit.origin==guard.origin&&exit.before_throw==guard.before_throw,Error::kCatchState);
      Require(std::holds_alternative<ValueLexicalStore>(store.operation)&&c_.operations[d.fallback_store.value]==d.local&&
          Same(Operand(d.fallback_store,OperandRole::kStoredValue),*read.result),Error::kBodyState);
      const auto& binary=Op(d.comparison);const auto* comparison=std::get_if<ValueBinary>(&binary.operation);
      Require(comparison&&binary.result&&Pure(binary)&&(comparison->op==BinaryOperator::kStrictEqual||comparison->op==BinaryOperator::kStrictNotEqual),Error::kParameterIdentity);
      const auto a=Operand(d.comparison,OperandRole::kLeft),b=Operand(d.comparison,OperandRole::kRight);
      Require((Same(a,*test.result)&&Same(b,SpecialValue::kUndefined))||(Same(b,*test.result)&&Same(a,SpecialValue::kUndefined)),Error::kParameterIdentity);
      const auto& branch=Op(d.branch);const auto* predicate=std::get_if<ValueBranch>(&branch.operation);
      Require(predicate&&Pure(branch)&&Same(Operand(d.branch,OperandRole::kCondition),*binary.result)&&Before(d.test,d.comparison)&&Before(d.comparison,d.branch),Error::kParameterIdentity);
      Require(d.missing.value<g_.edges.size()&&d.provided.value<g_.edges.size()&&d.missing!=d.provided,Error::kParameterIdentity);
      const bool positive=predicate->predicate==UnaryOperator::kTrueOrNonzeroNumber;
      Require(positive||predicate->predicate==UnaryOperator::kFalseOrZero,Error::kParameterIdentity);
      const bool taken=(comparison->op==BinaryOperator::kStrictEqual)==positive;
      for(const auto [edge,missing]:{std::pair{d.missing,true},std::pair{d.provided,false}}) {
        const auto& e=g_.edges[edge.value];failure_.location.edge=edge;
        Require(e.source==branch.block&&e.kind==((taken==missing)?FlowEdgeKind::kTaken:FlowEdgeKind::kFallthrough),Error::kParameterIdentity);
        Walk(e.target,d.join,[&](WorkBlockId block){
          Require(!region_[block.value],Error::kParameterOrder);region_[block.value]=static_cast<std::uint32_t>(2*i+(missing?1:2));
        });
      }
      for(const auto id:{d.read,d.guard,d.fallback_store})Require(region_[Op(id).block.value]==2*i+1,Error::kBodyState);
      Require(post_.Dominates({Op(d.read).block.value},{g_.edges[d.missing.value].target.value})&&
          post_.Dominates({store.block.value},{g_.edges[d.missing.value].target.value}),Error::kBodyState);
      for(const auto id:{d.initial_store,d.test,d.comparison,d.branch,d.read,d.guard,d.fallback_store}) {
        Require(!role_[id.value],Error::kCorrespondence);role_[id.value]=static_cast<std::uint32_t>(i+1);
      }
    }
    // One walk of the entire initialization region. Every original event is
    // either a proved role or an unobservable private initialization; the body
    // boundary is the final normal join, not a bytecode offset or opcode pattern.
    const auto body=defaults.back().join;
    Walk({0},body,[&](WorkBlockId b){prefix_[b.value]=1;});
    for(std::uint32_t i=0;i<g_.ops.size();++i) {
      Step();const OpId id{i};const auto& op=g_.ops[i];if(op.life!=ValueLife::kLive)continue;At(id);
      if(!prefix_[op.block.value]){Require(!role_[i],Error::kBodyState);continue;}
      if(role_[i])continue;
      if(std::holds_alternative<ValueEnvironmentPush>(op.operation)) {
        Require(op.use_count==0&&(!exit_[i]||!g_.exceptional_exits[*exit_[i]].handler),Error::kPrefixObservation);continue;
      }
      if(std::holds_alternative<ValueLexicalStore>(op.operation)) {
        const auto cell=c_.operations[i];Require(cell.has_value(),Error::kLocalCell);
        const auto& storage=c_.cells[cell->value];const auto* local=std::get_if<AllocatedCell>(&storage.storage);
        const auto value=Operand(id,OperandRole::kStoredValue);const auto* v=std::get_if<ValueId>(&value);
        const auto* entry=v?std::get_if<EntryDefinition>(&g_.values[v->value].origin):nullptr;
        Require(local&&Before(local->allocation,id)&&storage.count&&c_.accesses[storage.first].operation==id&&entry&&
            !std::holds_alternative<UnknownEntry>(*entry),Error::kPrefixObservation);
        continue;
      }
      Require(Pure(op),Error::kPrefixObservation);
      Require(!std::holds_alternative<ValueReturn>(op.operation)&&!std::holds_alternative<ValueThrow>(op.operation),Error::kBodyState);
    }
    // Canonical restricted-value uses are checked globally, including body and
    // phi users. Copies preserve identity but cannot hide an extra observation.
    std::vector<std::optional<std::uint32_t>> restricted;Size(restricted,g_.values.size());
    for(std::uint32_t i=0;i<defaults.size();++i){const auto& d=defaults[i];
      for(const auto v:{d.entry,*Op(d.test).result,*Op(d.read).result,*Op(d.comparison).result}) {
        Require(!restricted[v.value],Error::kAlias);restricted[v.value]=i;
      }}
    for(std::uint32_t i=0;i<g_.uses.size();++i) {
      Step();const auto& u=g_.uses[i];if(u.life!=ValueLife::kLive)continue;
      const auto value=Canon(u.operand);const auto* v=std::get_if<ValueId>(&value);if(!v||!restricted[v->value])continue;
      const auto& d=defaults[*restricted[v->value]];Context(d);failure_.use=UseId{i};
      const auto* owner=std::get_if<OpId>(&u.owner);Require(owner,Error::kPrefixObservation);At(*owner);
      if(std::holds_alternative<ValueCopy>(Op(*owner).operation)&&Pure(Op(*owner)))continue;
      bool allowed=false;
      if(*v==d.entry)allowed=*owner==d.initial_store&&u.role==OperandRole::kStoredValue;
      else if(*v==*Op(d.test).result)allowed=*owner==d.comparison;
      else if(*v==*Op(d.comparison).result)allowed=*owner==d.branch&&u.role==OperandRole::kCondition;
      else allowed=(*owner==d.guard&&u.role==OperandRole::kGuardValue)||(*owner==d.fallback_store&&u.role==OperandRole::kStoredValue);
      Require(allowed&&u.purpose==ReadPurpose::kOperand,Error::kPrefixObservation);
    }
    for(const auto& d:defaults) {
      Context(d);Require(prefix_[Op(d.destination.allocation).block.value]&&prefix_[Op(d.initial_store).block.value],Error::kLocalCell);
      const auto& local=c_.cells[d.local.value];
      for(std::uint32_t i=0;i<local.count;++i){Step();const auto a=c_.accesses[local.first+i];
        if(prefix_[Op(a.operation).block.value])Require(a.operation==d.initial_store||a.operation==d.test||a.operation==d.fallback_store,Error::kBodyState);
        else Require(dom_.Dominates({body.value},{Op(a.operation).block.value}),Error::kBodyState);
      }
    }
  }
 private:
  const ValueOp& Op(OpId id){Require(id.value<g_.ops.size(),Error::kCorrespondence);return g_.ops[id.value];}
  static bool Pure(const ValueOp& op){return op.normal==LanguageEffect::kNone&&op.before_throw==LanguageEffect::kNone&&op.exceptional==ExceptionalCompletion::kImpossible;}
  bool Before(OpId a,OpId b){const auto& x=Op(a);const auto& y=Op(b);return x.block==y.block?x.order<y.order:dom_.Dominates({x.block.value},{y.block.value});}
  ValueOperand Canon(const ValueOperand& v) const {const auto* id=std::get_if<ValueId>(&v);return id?canonical_[id->value]:v;}
  bool Same(const ValueOperand& a,const ValueOperand& b)const {
    const auto x=Canon(a),y=Canon(b);if(x.index()!=y.index())return false;
    if(const auto* v=std::get_if<ValueId>(&x))return *v==std::get<ValueId>(y);
    if(const auto* v=std::get_if<SpecialValue>(&x))return *v==std::get<SpecialValue>(y);
    if(const auto* v=std::get_if<IntegerLiteral>(&x))return v->value==std::get<IntegerLiteral>(y).value;
    return std::get<Float64Bits>(x).value==std::get<Float64Bits>(y).value;
  }
  ValueOperand Operand(OpId id,OperandRole role) {
    const auto& op=Op(id);std::optional<ValueOperand> result;
    for(std::uint32_t j=0;j<op.use_count;++j){Step();const auto& u=g_.uses[op.first_use.value+j];if(u.role==role){Require(!result,Error::kCorrespondence);result=Canon(u.operand);}}
    Require(result.has_value(),Error::kCorrespondence);return *result;
  }
  std::optional<OpId> Producer(const ValueOperand& v) const {const auto* id=std::get_if<ValueId>(&v);if(!id)return {};const auto* p=std::get_if<OpResult>(&g_.values[id->value].origin);return p?std::optional{p->op}:std::nullopt;}
  template<class Visit> void Walk(WorkBlockId start,WorkBlockId stop,Visit visit) {
    // Colors carry a generation, so disjoint regions cost their actual size.
    Require(generation_<=UINT32_MAX-2,Error::kResourceLimit);generation_+=2;stack_.clear();
    if(start==stop)return;stack_.push_back({start,0});walk_color_[start.value]=generation_;
    while(!stack_.empty()) {
      Step();auto& item=stack_.back();const auto& block=g_.blocks[item.first.value];
      Require(post_.Dominates({stop.value},{item.first.value}),Error::kBodyState);
      if(item.second==0)visit(item.first);
      if(item.second==block.edge_count){walk_color_[item.first.value]=generation_+1;stack_.pop_back();continue;}
      const auto target=g_.edges[block.first_edge.value+item.second++].target;if(target==stop)continue;
      Require(walk_color_[target.value]!=generation_,Error::kParameterOrder);
      if(walk_color_[target.value]==generation_+1)continue;
      walk_color_[target.value]=generation_;stack_.push_back({target,0});
    }
  }
  const ValueProgram& g_;const ValueEnvironments& e_;const CellPlan& c_;const ParameterPlan& p_;
  SemanticBudget initial_,left_;CellParameterDefaultFailure failure_;
  DominatorTree dom_,post_;
  std::vector<ValueOperand> canonical_;std::vector<std::uint8_t> colors_,prefix_;
  std::vector<ValueId> path_;std::vector<std::optional<OpId>> guards_,compare_branch_,load_compare_;
  std::vector<std::optional<std::uint32_t>> exit_;
  std::vector<std::uint32_t> role_,region_,walk_color_,guard_counts_;
  std::vector<std::pair<WorkBlockId,std::uint32_t>> stack_;std::uint32_t generation_=0;
};
}
std::variant<CellParameterDefaultProof,CellParameterDefaultFailure> CellParameterDefaultProof::Build(
    const analysis::ValueProgram& g,const analysis::ValueEnvironments& e,const CellPlan& c,
    const ParameterPlan& p,std::uint32_t length,analysis::SemanticBudget budget) {
  try {Analysis a(g,e,c,p,budget);a.Index();CellParameterDefaultProof proof;proof.defaults_=a.Discover(length);a.Verify(proof.defaults_,length);proof.cost_=a.Cost();proof.revision_=g.revision;proof.cfg_revision_=g.cfg_revision;return proof;}
  catch(const CellParameterDefaultFailure& f){return f;}
  catch(const std::bad_alloc&){return CellParameterDefaultFailure{CellParameterDefaultError::kResourceLimit};}
  catch(const std::length_error&){return CellParameterDefaultFailure{CellParameterDefaultError::kResourceLimit};}
}
std::variant<CellParameterDefaultCost,CellParameterDefaultFailure> VerifyCellParameterDefaults(
    const analysis::ValueProgram& g,const analysis::ValueEnvironments& e,const CellPlan& c,
    const ParameterPlan& p,std::uint32_t length,std::span<const CellParameterDefault> defaults,analysis::SemanticBudget budget) {
  try {Analysis a(g,e,c,p,budget);a.Index();a.Verify(defaults,length);return a.Cost();}
  catch(const CellParameterDefaultFailure& f){return f;}
  catch(const std::bad_alloc&){return CellParameterDefaultFailure{CellParameterDefaultError::kResourceLimit};}
  catch(const std::length_error&){return CellParameterDefaultFailure{CellParameterDefaultError::kResourceLimit};}
}
CellParameterDefaultDiagnostic DiagnoseCellParameterDefaults(const analysis::ValueProgram& g,
    const analysis::ValueEnvironments& e,const CellPlan& c,const ParameterPlan& p,std::uint32_t length,
    abc::EntityOffset method,analysis::SemanticBudget budget) {
  CellParameterDefaultDiagnostic d;
  auto result=CellParameterDefaultProof::Build(g,e,c,p,length,budget);
  if(const auto* f=std::get_if<CellParameterDefaultFailure>(&result))d.evidence=*f;
  else {
    const auto& proof=std::get<CellParameterDefaultProof>(result);const auto cost=proof.Cost();
    budget.bytes-=cost.allocated_bytes;budget.work-=cost.work;
    const auto checked=VerifyCellParameterDefaults(g,e,c,p,length,proof.Defaults(),budget);
    if(const auto* f=std::get_if<CellParameterDefaultFailure>(&checked))d.evidence=*f;
    else {d.verified=true;d.count=static_cast<std::uint32_t>(proof.Defaults().size());const auto& item=proof.Defaults().front();
      d.evidence.parameter=item.parameter;d.evidence.local=item.destination;d.evidence.outer=item.source;d.evidence.location=OperationLocation(g,item.branch,method);
      d.initial_store=OperationLocation(g,item.initial_store,method);d.test=OperationLocation(g,item.test,method);
      d.read=OperationLocation(g,item.read,method);d.guard=OperationLocation(g,item.guard,method);d.fallback_store=OperationLocation(g,item.fallback_store,method);d.body=item.join;
    }
  }
  d.evidence.location.method=method;return d;
}
}
