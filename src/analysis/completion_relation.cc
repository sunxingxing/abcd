#include "abcd/analysis/completion_relation.h"
#include "completion_values.h"
#include <algorithm>
#include <bit>
#include <set>
#include <stdexcept>

namespace abcd::analysis {
namespace {
using Value = detail::CompletionValue;
using Constant = detail::CompletionConstant;
constexpr auto kPhases = RawEventGraph::kPhases;
// Resource admission only: relational contexts can be dense. Admitted sparse
// states remain linear in graph/SSA/boundary input; exhaustion proves nothing.
constexpr std::size_t kStatesPerInput = 8;
constexpr std::uint64_t kWorkPerInput = 4096;
[[noreturn]] void Fail(CompletionRelationError kind,
    std::optional<ExceptionNodeId> node={}, std::optional<SsaValueId> value={}) {
  throw CompletionRelationFailure{kind,{},node,{},value};
}
std::uint32_t PhaseOffset(ExceptionPhase phase) {
  switch(phase) {
    case ExceptionPhase::kBefore:return 0;
    case ExceptionPhase::kCommit:return 1;
    case ExceptionPhase::kCatch:return 2;
  }
  Fail(CompletionRelationError::kInput);
}
struct PhiKey {
  std::uint32_t node,source;
  std::uint8_t successor;
  SsaValueId phi;
  auto operator<=>(const PhiKey&) const = default;
};
struct State {
  std::uint32_t node,entry,environment;
  auto operator<=>(const State&) const = default;
};
Value Literal(const ScalarValue& scalar) {
  Value value;
  if (const auto* i=std::get_if<IntegerLiteral>(&scalar)) {
    value.constant=Constant::kInteger;value.integer=i->value;
  } else if (const auto* special=std::get_if<SpecialValue>(&scalar)) {
    switch(*special) {
      case SpecialValue::kUndefined:value.constant=Constant::kUndefined;break;
      case SpecialValue::kNull:value.constant=Constant::kNull;break;
      case SpecialValue::kFalse:value.constant=Constant::kFalse;break;
      case SpecialValue::kTrue:value.constant=Constant::kTrue;break;
      case SpecialValue::kNaN:case SpecialValue::kInfinity:break;
    }
  }
  return value;
}
std::optional<bool> Truth(const Value& v,UnaryOperator op) {
  if (v.constant==Constant::kUnknown || v.constant==Constant::kHole) return {};
  const bool truth=v.constant==Constant::kTrue || (v.constant==Constant::kInteger && v.integer!=0);
  switch(op) {
    case UnaryOperator::kLogicalNot:return !truth;
    case UnaryOperator::kToBoolean:return truth;
    // JEQZ/JNEZ are exact boolean/number predicates, not complements on all
    // tagged values (branch_condition.cc). No object coercion is performed.
    case UnaryOperator::kFalseOrZero:
      return v.constant==Constant::kFalse || (v.constant==Constant::kInteger && v.integer==0);
    case UnaryOperator::kTrueOrNonzeroNumber:
      return v.constant==Constant::kTrue || (v.constant==Constant::kInteger && v.integer!=0);
    default:return {};
  }
}
std::optional<bool> Equal(const Value& a,const Value& b,bool strict) {
  if(a.constant==Constant::kUnknown || b.constant==Constant::kUnknown) return {};
  if(a.constant==Constant::kHole || b.constant==Constant::kHole) return {};
  if(a.constant==b.constant)
    return a.constant==Constant::kInteger ? a.integer==b.integer : true;
  if(strict) return false;
  if((a.constant==Constant::kNull && b.constant==Constant::kUndefined) ||
     (b.constant==Constant::kNull && a.constant==Constant::kUndefined)) return true;
  // Other loose coercions remain unknown; this is an abstract transfer, not
  // a scalar-operation admission whitelist or execution of user conversions.
  return {};
}
class Relation {
 public:
  Relation(const abc::MethodBody& body,abc::ProfileId profile,std::uint64_t frame,
      const ExceptionFlow& flow,const ExceptionSsa& ssa,
      std::span<const CompletionEntry> entries,std::span<const CompletionExit> exits,
      CompletionRelationBudget budget)
      : flow_(flow),ssa_(ssa),entries_(entries),arena_(budget.resources),raw_(body,arena_),
        values_(arena_),raw_to_flow_(arena_.Resource()),flow_to_raw_(arena_.Resource()),
        outgoing_(arena_.Resource()),phi_count_(arena_.Resource()),fallback_(arena_.Resource()),phi_index_(
          detail::CompletionLess<PhiKey>{&arena_},arena_.Resource()),
        exits_(detail::CompletionLess<CompletionExit>{&arena_},arena_.Resource()),
        seen_(detail::CompletionLess<State>{&arena_},arena_.Resource()),
        queue_(arena_.Resource()),leaving_(detail::CompletionLess<CompletionLeaving>{&arena_},arena_.Resource()) {
    for(const auto size:{body.instructions.size(),flow.nodes.size(),ssa.Values().size(),
        ssa.AllInputs().size(),entries.size(),exits.size()})
      if(size>UINT32_MAX) Fail(CompletionRelationError::kResourceLimit);
    const auto input=std::uint64_t(body.instructions.size())*kPhases+flow.nodes.size()+
        ssa.Values().size()+ssa.AllInputs().size()+entries.size()+exits.size()+1;
    if(input>UINT32_MAX/kStatesPerInput || entries.size()>UINT32_MAX) Fail(CompletionRelationError::kResourceLimit);
    limit_=std::min(budget.states,static_cast<std::size_t>(input)*kStatesPerInput);
    work_limit_=std::min(budget.resources.work,kWorkPerInput*input*(1+std::bit_width(input)));
    arena_.LimitWork(work_limit_);
    const auto dispatch=raw_.Select(profile);
    raw_.Allocate();raw_.Connect(dispatch,profile,frame);
    const auto nodes=raw_.Nodes();
    if(flow.instructions.size()!=body.instructions.size()) Fail(CompletionRelationError::kInput);
    raw_to_flow_.resize(nodes.size());flow_to_raw_.resize(flow.nodes.size());
    outgoing_.resize(nodes.size());phi_count_.resize(flow.nodes.size());fallback_.resize(ssa.Values().size());
    for(std::uint32_t n=0;n<flow.nodes.size();++n) {
      arena_.Step();const auto& node=flow.nodes[n];
      if(node.instruction.value>=flow.instructions.size() || node.successor_count>node.successors.size())
        Fail(CompletionRelationError::kInput,ExceptionNodeId{n});
      const auto r=node.instruction.value*kPhases+PhaseOffset(node.phase);
      if(raw_to_flow_[r]) Fail(CompletionRelationError::kInput,ExceptionNodeId{n});
      if(flow.instructions[node.instruction.value].pc!=body.instructions[node.instruction.value].offset)
        Fail(CompletionRelationError::kInput,ExceptionNodeId{n});
      raw_to_flow_[r]=ExceptionNodeId{n};flow_to_raw_[n]=r;
    }
    for(std::uint32_t r=0;r<nodes.size();++r) {
      arena_.Step();const auto& node=nodes[r];
      if(node.phase==RawEventPhase::kCallerExit || (node.phase==RawEventPhase::kCatch && !raw_to_flow_[r])) continue;
      if(!raw_to_flow_[r]) Fail(CompletionRelationError::kInput);
      const auto id=*raw_to_flow_[r];const auto& original=flow.nodes[id.value];
      std::array<bool,kMaxNormalSuccessors> matched{};
      for(std::uint8_t j=0;j<node.count;++j) {
        arena_.Step(); const auto target=node.next[j];
        if(nodes[target].phase==RawEventPhase::kCallerExit) {
          const auto& op=flow.instructions[original.instruction.value].operation;
          if(!((original.phase==ExceptionPhase::kBefore && original.exception && !original.exception->target) ||
              (original.phase==ExceptionPhase::kCommit && std::holds_alternative<ReturnOperation>(op))))
            Fail(CompletionRelationError::kInput,id);
          outgoing_[r][j]=CompletionCallerExit{id};continue;
        }
        bool found=false;
        for(std::uint8_t e=0;e<original.successor_count;++e) {
          arena_.Step();const auto& edge=original.successors[e];
          if(edge.target.value>=flow_to_raw_.size()) Fail(CompletionRelationError::kInput,id);
          if(!matched[e] && flow_to_raw_[edge.target.value]==target) {
            matched[e]=true;outgoing_[r][j]=CompletionEdge{id,e};found=true;break;
          }
        }
        if(!found) Fail(CompletionRelationError::kInput,id);
      }
      for(std::uint8_t e=0;e<original.successor_count;++e)
        if(!matched[e]) Fail(CompletionRelationError::kInput,id);
    }
    for(const auto& exit:exits) {
      arena_.Step();(void)Edge(exit);
      if(!exits_.insert(exit).second) Fail(CompletionRelationError::kExit);
    }
    IndexValues();
    for(const auto& e:entries_) {
      arena_.Step();(void)Edge(CompletionExit{e.edge});CheckPending(e.pending);
      if(e.binding && e.binding->value>=ssa_.Values().size()) Fail(CompletionRelationError::kPayload,{},e.binding);
    }
    AvailableEntries();
  }
  CompletionRelationCost Verify(const CompletionRelation& supplied) {
    const auto expected=Run(true);
    if(expected.arrivals.size()!=supplied.arrivals.size() || expected.leaving.size()!=supplied.leaving.size())
      Fail(CompletionRelationError::kCorrespondence);
    for(std::size_t i=0;i<expected.arrivals.size();++i) {
      arena_.Step();if(expected.arrivals[i]!=supplied.arrivals[i]) Fail(CompletionRelationError::kCorrespondence);
    }
    for(std::size_t i=0;i<expected.leaving.size();++i) {
      arena_.Step();if(expected.leaving[i]!=supplied.leaving[i]) Fail(CompletionRelationError::kCorrespondence);
    }
    return {arena_.Peak(),seen_.size(),arena_.Work()};
  }
  CompletionRelation Run(bool reverse) {
    try { return RunImpl(reverse); }
    catch(CompletionRelationFailure failure) {
      if(!failure.entry) failure.entry=current_;
      throw failure;
    }
  }
  CompletionRelation RunImpl(bool reverse) {
    for(std::uint32_t e=0;e<entries_.size();++e) {
      arena_.Step();current_=e;
      auto root=std::uint32_t{0};const auto& entry=entries_[e];
      if(const auto* id=std::get_if<SsaValueId>(&entry.pending.payload)) {
        auto v=fallback_[id->value];const auto canonical=std::get_if<SsaValueId>(&v.payload);
        const auto base=canonical?*canonical:*id;v.payload=entry.pending.payload;
        root=values_.Set(root,base,v);root=values_.Set(root,*id,v);
      }
      if(entry.binding) {
        auto value=fallback_[entry.binding->value];
        const auto* canonical=std::get_if<SsaValueId>(&value.payload);
        const auto base=canonical?*canonical:*entry.binding;
        value.payload=entry.pending.payload;
        root=values_.Set(root,base,value);root=values_.Set(root,*entry.binding,value);
      }
      const auto arc=Edge(CompletionExit{entry.edge});
      Arrive({arc.first,e,root},arc.second);
    }
    std::size_t cursor=0;
    while(reverse ? !queue_.empty() : cursor<queue_.size()) {
      arena_.Step();auto state=reverse?queue_.back():queue_[cursor++];
      if(reverse) queue_.pop_back();current_=state.entry;
      const auto node=*raw_to_flow_[state.node];
      const auto& original=flow_.nodes[node.value];
      const auto& op=flow_.instructions[original.instruction.value].operation;
      if(raw_.Nodes()[state.node].unknown) Fail(CompletionRelationError::kUnknownEffect,node);
      if(original.phase==ExceptionPhase::kCommit) {
        if(const auto write=ssa_.Write(node)) {
          if(write->value>=ssa_.Values().size()) Fail(CompletionRelationError::kInput,node,*write);
          Value value{*write};
          if(const auto* scalar=std::get_if<ScalarAssignment>(&op))
            value=Expression(scalar->expression,original.instruction,state.environment,*write);
          else if(std::holds_alternative<LoadHole>(op)) value.constant=Constant::kHole;
          state.environment=values_.Set(state.environment,*write,value);
        }
      }
      std::optional<bool> branch;
      if(original.phase==ExceptionPhase::kCommit)
        if(const auto* b=std::get_if<BranchOperation>(&op))
          branch=Truth(Scalar(b->predicate.input,original.instruction,state.environment),b->predicate.op);
      const auto& raw_node=raw_.Nodes()[state.node];
      for(std::uint8_t i=0;i<raw_node.count;++i) {
        arena_.Step();const auto exit=*outgoing_[state.node][i];
        if(branch) {
          const auto* edge=std::get_if<CompletionEdge>(&exit);
          if(!edge) Fail(CompletionRelationError::kInput,node);
          const auto taken=flow_.nodes[edge->source.value].successors[edge->successor].kind==ExceptionArcKind::kTaken;
          if(taken!=*branch) continue;
        }
        if(exits_.contains(exit)) {
          auto pending=entries_[state.entry].pending;
          if(IsException(exit)) {
            CompletionTarget target=CompletionCaller{};
            if(const auto* e=std::get_if<CompletionEdge>(&exit))
              target=flow_.nodes[e->source.value].successors[e->successor].target;
            pending={PendingCompletionKind::kThrow,Exception(state).payload,target};
          }
          else if(original.phase==ExceptionPhase::kCommit)
            if(const auto* ret=std::get_if<ReturnOperation>(&op)) {
              const auto value=Scalar(ret->value,original.instruction,state.environment);
              if(std::holds_alternative<std::monostate>(value.payload) && value.constant!=Constant::kUndefined)
                Fail(CompletionRelationError::kPayload,node);
              pending={PendingCompletionKind::kReturn,value.payload,CompletionCaller{}};
            }
          CheckPending(pending);
          leaving_.insert({state.entry,exit,pending});
        } else {
          if(std::holds_alternative<CompletionCallerExit>(exit)) Fail(CompletionRelationError::kExit,node);
          Arrive(state,i);
        }
      }
    }
    CompletionRelation result;
    Reserve(result.arrivals,entries_.size());Reserve(result.leaving,leaving_.size());
    for(std::uint32_t e=0;e<entries_.size();++e) result.arrivals.push_back({e,entries_[e].pending});
    result.leaving.assign(leaving_.begin(),leaving_.end());
    result.peak_bytes=arena_.Peak();result.states=seen_.size();result.work=arena_.Work();
    return result;
  }
 private:
  template<class T> void Reserve(std::vector<T>& out,std::size_t size) {
    if(size>arena_.Available()/sizeof(T)) Fail(CompletionRelationError::kResourceLimit);
    arena_.Retain(size*sizeof(T));out.reserve(size);
    if(out.capacity()!=size) Fail(CompletionRelationError::kResourceLimit);
  }
  void AvailableEntries() {
    const bool needed=std::any_of(entries_.begin(),entries_.end(),[](const auto& e){
      return e.binding || std::holds_alternative<SsaValueId>(e.pending.payload);
    });
    if(!needed) return;
    std::pmr::vector<std::uint32_t> offsets(arena_.Resource());
    std::pmr::vector<BlockId> edges(arena_.Resource());
    offsets.push_back(0);
    for(const auto& node:flow_.nodes) {
      arena_.Step();
      for(std::uint8_t e=0;e<node.successor_count;++e) {
        arena_.Step();if(edges.size()==UINT32_MAX) Fail(CompletionRelationError::kResourceLimit);
        edges.push_back({node.successors[e].target.value});
      }
      offsets.push_back(static_cast<std::uint32_t>(edges.size()));
    }
    // The delegated solver precharges all its buffers against this exclusive
    // remaining grant. Conservatively retain its whole peak for this query;
    // never credit still-alive CSR, tree or solver scratch prematurely.
    auto built=BuildDominators({offsets,edges},{0},{arena_.Available(),work_limit_-arena_.Work()});
    if(const auto* e=std::get_if<DominatorError>(&built)) {
      if(*e==DominatorError::kResourceLimit) Fail(CompletionRelationError::kResourceLimit);
      if(*e==DominatorError::kWorkLimit) Fail(CompletionRelationError::kWorkLimit);
      Fail(CompletionRelationError::kInput);
    }
    const auto& tree=std::get<DominatorTree>(built);
    arena_.Retain(tree.work.allocated_bytes);arena_.Step(tree.work.steps);
    const auto available=[&](SsaValueId value,ExceptionNodeId source) {
      const auto& origin=ssa_.Values()[value.value].origin;
      std::optional<BlockId> definition;
      if(const auto* instruction=std::get_if<InstructionValue>(&origin)) definition=BlockId{instruction->instruction.value};
      if(const auto* phi=std::get_if<PhiValue>(&origin)) definition=phi->block;
      if(!tree.nodes[source.value].begin || (definition && !tree.Dominates(*definition,{source.value})))
        Fail(CompletionRelationError::kPayload,source,value);
    };
    for(std::uint32_t i=0;i<entries_.size();++i) {
      arena_.Step();const auto& entry=entries_[i];
      try {
        if(const auto* value=std::get_if<SsaValueId>(&entry.pending.payload)) available(*value,entry.edge.source);
        if(entry.binding) available(*entry.binding,entry.edge.source);
      } catch(CompletionRelationFailure failure) {failure.entry=i;failure.edge=entry.edge;throw failure;}
    }
  }
  void CheckPending(const PendingCompletion& p) {
    switch(p.kind) {
      case PendingCompletionKind::kNormal:case PendingCompletionKind::kBreak:case PendingCompletionKind::kContinue:
        if(!std::holds_alternative<std::monostate>(p.payload)) Fail(CompletionRelationError::kPayload);break;
      case PendingCompletionKind::kReturn:break;
      case PendingCompletionKind::kThrow:
        if(std::holds_alternative<std::monostate>(p.payload)) Fail(CompletionRelationError::kPayload);break;
    }
    if(const auto* id=std::get_if<SsaValueId>(&p.payload);id && id->value>=ssa_.Values().size())
      Fail(CompletionRelationError::kPayload,{},*id);
    if(const auto* id=std::get_if<CompletionExceptionValue>(&p.payload)) {
      if(id->before.value>=flow_.nodes.size() || !flow_.nodes[id->before.value].exception)
        Fail(CompletionRelationError::kPayload,id->before);
    }
    if(const auto* id=std::get_if<ExceptionNodeId>(&p.target);id && id->value>=flow_.nodes.size())
      Fail(CompletionRelationError::kInput,*id);
  }
  std::pair<std::uint32_t,std::uint8_t> Edge(const CompletionExit& exit) {
    const auto source=std::visit([](const auto& e){return e.source;},exit);
    if(source.value>=flow_to_raw_.size()) Fail(CompletionRelationError::kInput,source);
    const auto r=flow_to_raw_[source.value];
    for(std::uint8_t j=0;j<raw_.Nodes()[r].count;++j) {
      arena_.Step();if(outgoing_[r][j] && *outgoing_[r][j]==exit) return {r,j};
    }
    Fail(CompletionRelationError::kInput,source);
  }
  SsaValueId Read(InstructionId instruction,const ScalarSlot& slot) {
    const auto before=raw_to_flow_[instruction.value*kPhases];
    if(!before) Fail(CompletionRelationError::kInput);
    const auto& effects=flow_.instructions[instruction.value].effects.normal_registers;
    const auto reads=ssa_.Reads(*before);
    if(effects.read_count>effects.reads.size()) Fail(CompletionRelationError::kInput,*before);
    for(std::size_t i=0;i<effects.read_count;++i) {
      arena_.Step();
      const bool same=slot.index()==effects.reads[i].index() &&
          (std::holds_alternative<Accumulator>(slot) || std::get<abc::RegisterId>(slot)==std::get<abc::RegisterId>(effects.reads[i]));
      if(same) {
        if(i>=reads.size() || reads[i].ordinal!=i || reads[i].value.value>=ssa_.Values().size())
          Fail(CompletionRelationError::kInput,*before);
        return reads[i].value;
      }
    }
    Fail(CompletionRelationError::kInput,*before);
  }
  void IndexValues() {
    std::pmr::vector<std::optional<SsaValueId>> copy(ssa_.Values().size(),{},arena_.Resource());
    for(std::uint32_t v=0;v<ssa_.Values().size();++v) {
      arena_.Step();fallback_[v].payload=SsaValueId{v};
      if(const auto* phi=std::get_if<PhiValue>(&ssa_.Values()[v].origin)) {
        if(phi->block.value>=phi_count_.size()) Fail(CompletionRelationError::kInput);
        ++phi_count_[phi->block.value];
      }
      if(const auto* source=std::get_if<InstructionValue>(&ssa_.Values()[v].origin)) {
        if(source->instruction.value>=flow_.nodes.size()) Fail(CompletionRelationError::kInput);
        const auto& n=flow_.nodes[source->instruction.value];
        if(n.phase==ExceptionPhase::kCatch) continue;
        if(n.phase!=ExceptionPhase::kCommit) Fail(CompletionRelationError::kInput);
        const auto& op=flow_.instructions[n.instruction.value].operation;
        if(std::holds_alternative<LoadHole>(op)) fallback_[v].constant=Constant::kHole;
        if(const auto* assignment=std::get_if<ScalarAssignment>(&op))
          if(const auto* scalar=std::get_if<ScalarValue>(&assignment->expression)) {
            if(const auto* reg=std::get_if<abc::RegisterId>(scalar)) copy[v]=Read(n.instruction,*reg);
            else if(std::holds_alternative<Accumulator>(*scalar)) copy[v]=Read(n.instruction,Accumulator{});
            else {fallback_[v]=Literal(*scalar);fallback_[v].payload=SsaValueId{v};}
          }
      }
    }
    std::pmr::vector<std::uint8_t> color(copy.size(),0,arena_.Resource());
    std::pmr::vector<std::uint32_t> stack(arena_.Resource());
    for(std::uint32_t v=0;v<copy.size();++v) {
      auto next=v;
      while(color[next]==0) {
        arena_.Step();color[next]=1;stack.push_back(next);
        if(!copy[next]) break;
        next=copy[next]->value;
      }
      if(copy[stack.empty()?v:stack.back()] && color[next]==1) Fail(CompletionRelationError::kInput);
      while(!stack.empty()) {
        arena_.Step();const auto id=stack.back();stack.pop_back();
        if(copy[id]) fallback_[id]=fallback_[copy[id]->value];
        color[id]=2;
      }
    }
    for(const auto& arm:ssa_.AllInputs()) {
      arena_.Step();if(arm.phi.value>=ssa_.Values().size() || arm.value.value>=ssa_.Values().size())
        Fail(CompletionRelationError::kInput);
      const auto* phi=std::get_if<PhiValue>(&ssa_.Values()[arm.phi.value].origin);
      if(!phi || phi->block.value>=flow_.nodes.size()) Fail(CompletionRelationError::kPhi,{},arm.phi);
      if(const auto* e=std::get_if<SsaEdge>(&arm.edge)) {
        if(e->source.value>=flow_.nodes.size()) Fail(CompletionRelationError::kPhi,{},arm.phi);
        if(!phi_index_.emplace(PhiKey{phi->block.value,e->source.value,e->successor,arm.phi},arm.value).second)
          Fail(CompletionRelationError::kPhi,{},arm.phi);
      }
    }
  }
  Value Get(SsaValueId v,std::uint32_t root) {
    arena_.Step();if(v.value>=fallback_.size()) Fail(CompletionRelationError::kPayload,{},v);
    if(auto value=values_.Get(root,v)) return *value;
    const auto fallback=fallback_[v.value];
    if(const auto* base=std::get_if<SsaValueId>(&fallback.payload))
      if(auto value=values_.Get(root,*base)) return *value;
    return fallback;
  }
  Value Scalar(const ScalarValue& value,InstructionId instruction,std::uint32_t root) {
    if(const auto* r=std::get_if<abc::RegisterId>(&value)) return Get(Read(instruction,*r),root);
    if(std::holds_alternative<Accumulator>(value)) return Get(Read(instruction,Accumulator{}),root);
    return Literal(value);
  }
  Value Expression(const ScalarExpression& expression,InstructionId instruction,std::uint32_t root,SsaValueId destination) {
    if(const auto* scalar=std::get_if<ScalarValue>(&expression)) {
      auto value=Scalar(*scalar,instruction,root);
      if(!std::holds_alternative<Accumulator>(*scalar) && !std::holds_alternative<abc::RegisterId>(*scalar)) value.payload=destination;
      return value;
    }
    Value result{destination};std::optional<bool> boolean;
    if(const auto* unary=std::get_if<UnaryExpression>(&expression))
      boolean=Truth(Scalar(unary->input,instruction,root),unary->op);
    if(const auto* binary=std::get_if<BinaryExpression>(&expression)) {
      switch(binary->op) {
        case BinaryOperator::kEqual:case BinaryOperator::kNotEqual:
        case BinaryOperator::kStrictEqual:case BinaryOperator::kStrictNotEqual:
          boolean=Equal(Scalar(binary->left,instruction,root),Scalar(binary->right,instruction,root),
              binary->op==BinaryOperator::kStrictEqual || binary->op==BinaryOperator::kStrictNotEqual);
          if(boolean && (binary->op==BinaryOperator::kNotEqual || binary->op==BinaryOperator::kStrictNotEqual)) boolean=!*boolean;
          break;
        default:break;
      }
    }
    if(boolean) result.constant=*boolean?Constant::kTrue:Constant::kFalse;
    return result;
  }
  bool IsException(const CompletionExit& exit) const {
    if(const auto* e=std::get_if<CompletionEdge>(&exit))
      return flow_.nodes[e->source.value].successors[e->successor].kind==ExceptionArcKind::kException;
    return flow_.nodes[std::get<CompletionCallerExit>(exit).source.value].phase==ExceptionPhase::kBefore;
  }
  Value Exception(const State& state) {
    const auto node=*raw_to_flow_[state.node];const auto instruction=flow_.nodes[node.value].instruction;
    if(const auto* thrown=std::get_if<ThrowOperation>(&flow_.instructions[instruction.value].operation))
      return Scalar(thrown->value,instruction,state.environment);
    return {CompletionExceptionValue{node}};
  }
  void Arrive(State state,std::uint8_t successor) {
    const auto from=*raw_to_flow_[state.node];const auto exit=*outgoing_[state.node][successor];
    const auto* edge=std::get_if<CompletionEdge>(&exit);
    if(!edge) Fail(CompletionRelationError::kEntry,from);
    const auto target=flow_.nodes[edge->source.value].successors[edge->successor].target;
    auto root=state.environment;
    const PhiKey first{target.value,from.value,edge->successor,{0}};
    std::uint32_t covered=0;
    for(auto at=phi_index_.lower_bound(first);at!=phi_index_.end() && at->first.node==first.node &&
        at->first.source==first.source && at->first.successor==first.successor;++at) {
      arena_.Step();++covered;root=values_.Set(root,at->first.phi,Get(at->second,state.environment));
    }
    if(covered!=phi_count_[target.value]) Fail(CompletionRelationError::kPhi,target);
    if(flow_.nodes[target.value].phase==ExceptionPhase::kCatch) {
      if(!IsException(exit)) Fail(CompletionRelationError::kInput,target);
      const auto write=ssa_.Write(target);if(!write) Fail(CompletionRelationError::kInput,target);
      root=values_.Set(root,*write,Exception(state));
    }
    State next{flow_to_raw_[target.value],state.entry,root};
    if(seen_.contains(next)) return;
    if(seen_.size()>=limit_) Fail(CompletionRelationError::kResourceLimit,target);
    seen_.insert(next);queue_.push_back(next);
  }
  const ExceptionFlow& flow_;const ExceptionSsa& ssa_;
  std::span<const CompletionEntry> entries_;
  RawEventArena arena_;RawEventGraph raw_;detail::CompletionValues values_;
  std::pmr::vector<std::optional<ExceptionNodeId>> raw_to_flow_;
  std::pmr::vector<std::uint32_t> flow_to_raw_;
  std::pmr::vector<std::array<std::optional<CompletionExit>,3>> outgoing_;
  std::pmr::vector<std::uint32_t> phi_count_;
  std::pmr::vector<Value> fallback_;
  std::pmr::map<PhiKey,SsaValueId,detail::CompletionLess<PhiKey>> phi_index_;
  std::pmr::set<CompletionExit,detail::CompletionLess<CompletionExit>> exits_;
  std::pmr::set<State,detail::CompletionLess<State>> seen_;
  std::pmr::vector<State> queue_;
  std::pmr::set<CompletionLeaving,detail::CompletionLess<CompletionLeaving>> leaving_;
  std::size_t limit_=0;
  std::uint64_t work_limit_=0;
  std::optional<std::uint32_t> current_;
};
CompletionRelationFailure RawFailure(const RawEventFailure& f) {
  switch(f.kind) {
    case RawEventError::kResourceLimit:return {CompletionRelationError::kResourceLimit};
    case RawEventError::kWorkLimit:return {CompletionRelationError::kWorkLimit};
    default:return {CompletionRelationError::kInput};
  }
}
}
std::variant<CompletionRelation,CompletionRelationFailure> AnalyzeCompletionRelation(
    const abc::MethodBody& body,abc::ProfileId profile,std::uint64_t frame,
    const ExceptionFlow& flow,const ExceptionSsa& ssa,std::span<const CompletionEntry> entries,
    std::span<const CompletionExit> exits,CompletionRelationBudget budget) {
  try {return Relation(body,profile,frame,flow,ssa,entries,exits,budget).Run(false);}
  catch(const CompletionRelationFailure& f){return f;}
  catch(const RawEventFailure& f){return RawFailure(f);}
  catch(const std::bad_alloc&){return CompletionRelationFailure{CompletionRelationError::kResourceLimit};}
  catch(const std::length_error&){return CompletionRelationFailure{CompletionRelationError::kResourceLimit};}
}
std::variant<CompletionRelationCost,CompletionRelationFailure> VerifyCompletionRelation(
    const abc::MethodBody& body,abc::ProfileId profile,std::uint64_t frame,
    const ExceptionFlow& flow,const ExceptionSsa& ssa,std::span<const CompletionEntry> entries,
    std::span<const CompletionExit> exits,const CompletionRelation& relation,CompletionRelationBudget budget) {
  try {
    return Relation(body,profile,frame,flow,ssa,entries,exits,budget).Verify(relation);
  } catch(const CompletionRelationFailure& f){return f;}
  catch(const RawEventFailure& f){return RawFailure(f);}
  catch(const std::bad_alloc&){return CompletionRelationFailure{CompletionRelationError::kResourceLimit};}
  catch(const std::length_error&){return CompletionRelationFailure{CompletionRelationError::kResourceLimit};}
}
}  // namespace abcd::analysis
