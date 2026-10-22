#include "object_rest_plan.h"
#include "effect_order.h"
#include "../analysis/value_edits.h"
#include <algorithm>
#include <bit>
#include <array>
namespace abcd::source {
namespace {
using namespace analysis;
constexpr auto kProof=TreeStructureError::kUnsupportedSyntax;
ValueOperand Canonical(const EvaluationPlan& p,ValueOperand v){if(auto* id=std::get_if<ValueId>(&v);id&&!p.aliases.empty())*id=p.aliases[id->value];return v;}
ValueOperand Input(const ValueProgram& g,const EvaluationPlan& p,OpId op,std::uint32_t n){const auto& o=g.ops[op.value];RequireTree(n<o.use_count,kProof);return Canonical(p,g.uses[o.first_use.value+n].operand);}
bool Same(ValueOperand a,ValueOperand b){if(a.index()!=b.index())return false;return std::visit([&](const auto& v){using T=std::decay_t<decltype(v)>;if constexpr(std::is_same_v<T,SpecialValue>)return v==std::get<T>(b);else return v.value==std::get<T>(b).value;},a);}
std::optional<abc::PoolIndex> String(const ValueProgram& g,ValueOperand v){if(const auto* id=std::get_if<ValueId>(&v))if(const auto* r=std::get_if<OpResult>(&g.values[id->value].origin))if(const auto* text=std::get_if<ValueString>(&g.ops[r->op.value].operation))if(text->kind==PoolTextKind::kString)return text->string;return {};}
bool SameKey(const ValueProgram& g,ValueOperand a,ValueOperand c){if(Same(a,c))return true;auto x=String(g,a),y=String(g,c);return x&&y&&x->value==y->value;}
std::optional<SpecialValue> Special(const ValueProgram& g,const EvaluationPlan& p,ValueOperand v){
  if(const auto* s=std::get_if<SpecialValue>(&v))return *s;
  if(const auto* id=std::get_if<ValueId>(&v))if(const auto* r=std::get_if<OpResult>(&g.values[id->value].origin)) {
    const auto& o=g.ops[r->op.value];if(std::holds_alternative<ValueCopy>(o.operation)&&o.use_count==1)if(const auto* s=std::get_if<SpecialValue>(&g.uses[o.first_use.value].operand))return *s;
  }
  return {};
}
struct Fact {ValueId source;std::uint8_t mask=0;};
// Shared edge-split dominance carries null/undefined exclusion facts. Real
// exception edges participate: a successful Get is not assumed on its Catch.
std::vector<std::uint8_t> NonNull(const ValueProgram& g,const EvaluationPlan& p,TreeCheckBudget& b) {
  const auto n=g.blocks.size(), e=g.edges.size();RequireTree(n+e<UINT32_MAX,kProof);
  std::vector<std::uint32_t> offsets,cursors;std::vector<BlockId> targets;
  b.Array(offsets,n+e+1);b.Array(cursors,n+e);b.Array(targets,2*e);
  for(const auto& edge:g.edges){b.Step();++offsets[edge.source.value+1];}
  for(std::size_t i=0;i<e;++i)offsets[n+i+1]=1;
  for(std::size_t i=1;i<offsets.size();++i){b.Step();offsets[i]+=offsets[i-1];}
  std::copy(offsets.begin(),offsets.end()-1,cursors.begin());
  for(std::uint32_t i=0;i<e;++i){b.Step();const auto& edge=g.edges[i];targets[cursors[edge.source.value]++]={static_cast<std::uint32_t>(n+i)};targets[cursors[n+i]++]={edge.target.value};}
  auto built=BuildDominators({offsets,targets},{},{b.Remaining().bytes,b.Remaining().work});
  if(const auto* error=std::get_if<DominatorError>(&built))throw *error==DominatorError::kWorkLimit?TreeStructureError::kWorkLimit:*error==DominatorError::kResourceLimit?TreeStructureError::kResourceLimit:kProof;
  auto dom=std::get<DominatorTree>(std::move(built));b.Admit(dom.work.allocated_bytes,1);b.Step(dom.work.steps);
  std::vector<Fact> facts; b.Array(facts,e);
  for(std::uint32_t i=0;i<e;++i){b.Step();const auto& edge=g.edges[i];const auto& block=g.blocks[edge.source.value];if((g.phases.empty()&&!block.op_count)||(edge.kind!=FlowEdgeKind::kTaken&&edge.kind!=FlowEdgeKind::kFallthrough))continue;
    OpId id{};
    if(!g.phases.empty()) {
      const auto& phase=g.phases[edge.source.value];
      if(phase.phase!=ExceptionPhase::kCommit||!phase.operation)continue;
      id=*phase.operation;
    } else id={block.first_op.value+block.op_count-1};
    const auto* branch=std::get_if<ValueBranch>(&g.ops[id.value].operation);if(!branch)continue;
    bool positive=edge.kind==FlowEdgeKind::kTaken;
    switch(branch->predicate){case UnaryOperator::kToBoolean:case UnaryOperator::kTrueOrNonzeroNumber:break;case UnaryOperator::kLogicalNot:case UnaryOperator::kFalseOrZero:positive=!positive;break;default:continue;}
    auto value=Input(g,p,id,0);const auto* v=std::get_if<ValueId>(&value);if(!v)continue;const auto* result=std::get_if<OpResult>(&g.values[v->value].origin);if(!result)continue;
    const auto& test=g.ops[result->op.value];const auto* equal=std::get_if<ValueBinary>(&test.operation);if(!equal)continue;
    bool strict=false;switch(equal->op){case BinaryOperator::kStrictEqual:strict=true;break;case BinaryOperator::kEqual:break;case BinaryOperator::kStrictNotEqual:strict=true;positive=!positive;break;case BinaryOperator::kNotEqual:positive=!positive;break;default:continue;}
    if(positive)continue;auto left=Input(g,p,result->op,0),right=Input(g,p,result->op,1);auto constant=Special(g,p,right);if(!constant){std::swap(left,right);constant=Special(g,p,right);}if(!constant)continue;
    const auto* source=std::get_if<ValueId>(&left);if(!source)continue;std::uint8_t mask=0;if(*constant==SpecialValue::kNull)mask=strict?1:3;if(*constant==SpecialValue::kUndefined)mask=strict?2:3;if(mask)facts[i]={*source,mask};
  }
  // Sort entry/leave events by dominator interval; rollback facts on leaving.
  struct Event{std::uint32_t at;bool leave;Fact fact;};std::vector<Event> events;b.Array(events,2*e);std::size_t count=0;
  for(std::uint32_t i=0;i<e;++i)if(facts[i].mask&&dom.nodes[n+i].begin){b.Step();events[count++]={dom.nodes[n+i].begin,false,facts[i]};events[count++]={dom.nodes[n+i].end,true,facts[i]};}events.resize(count);
  std::sort(events.begin(),events.end(),[&](const Event& a,const Event& c){b.Step();if(a.at!=c.at)return a.at<c.at;return a.leave>c.leave;});
  std::vector<std::array<std::uint32_t,2>> active;b.Array(active,g.values.size());std::vector<std::uint32_t> ordered;b.Array(ordered,n);for(std::uint32_t i=0;i<n;++i)ordered[i]=i;std::sort(ordered.begin(),ordered.end(),[&](auto a,auto c){b.Step();return dom.nodes[a].begin<dom.nodes[c].begin;});
  std::vector<std::uint8_t> result;b.Array(result,g.ops.size());std::size_t at=0;
  for(const auto block:ordered){b.Step();const auto entry=dom.nodes[block].begin;while(at<events.size()&&events[at].at<=entry){b.Step();const auto& event=events[at++];for(unsigned bit=0;bit<2;++bit)if(event.fact.mask&(1U<<bit)){auto& x=active[event.fact.source.value][bit];if(event.leave){RequireTree(x>0,kProof);--x;}else ++x;}}
    const auto& body=g.blocks[block];for(std::uint32_t i=0;i<body.op_count;++i){b.Step();OpId op{body.first_op.value+i};const auto& operation=g.ops[op.value];if(operation.life!=ValueLife::kLive||(!std::holds_alternative<ValueObjectRest>(operation.operation)&&!std::holds_alternative<ValueNamedRead>(operation.operation)&&!std::holds_alternative<ValuePropertyRead>(operation.operation)))continue;auto input=Input(g,p,op,0);if(const auto* source=std::get_if<ValueId>(&input)){const auto domain=g.values[source->value].domain;const auto intrinsic=(domain.IsString()||domain.IsNumber()||domain.IsBoolean())?3:domain.IsDefined()?2:0;result[op.value]=intrinsic|(active[source->value][0]?1:0)|(active[source->value][1]?2:0);}}
  }
  return result;
}
}
void PinObjectRests(const analysis::ValueProgram& g,const EvaluationPlan& p,std::span<std::uint8_t> pinned,TreeCheckBudget& b){
  bool found=false;for(const auto& op:g.ops){b.Step();found|=op.life==ValueLife::kLive&&std::holds_alternative<ValueObjectRest>(op.operation);}if(!found)return;
  std::vector<std::uint8_t> bases;b.Array(bases,g.values.size());for(std::uint32_t i=0;i<g.ops.size();++i){b.Step();if(g.ops[i].life!=ValueLife::kLive||!std::holds_alternative<ValueObjectRest>(g.ops[i].operation))continue;auto source=Input(g,p,{i},0);if(const auto* v=std::get_if<ValueId>(&source))bases[v->value]=1;pinned[i]=1;
    // Exclusion registers are already evaluated values. Their producers must
    // remain at their original events even when the sentinel ignores the list;
    // native pattern rendering never evaluates a hidden operand subtree.
    for(std::uint32_t k=1;k<g.ops[i].use_count;++k){b.Step();auto key=Input(g,p,{i},k);if(const auto* v=std::get_if<ValueId>(&key))if(const auto* producer=std::get_if<OpResult>(&g.values[v->value].origin))pinned[producer->op.value]=1;}
}
  for(std::uint32_t i=0;i<g.ops.size();++i){b.Step();const auto& op=g.ops[i];if(op.life!=ValueLife::kLive||(!std::holds_alternative<ValueNamedRead>(op.operation)&&!std::holds_alternative<ValuePropertyRead>(op.operation)))continue;auto source=Input(g,p,{i},0);if(const auto* v=std::get_if<ValueId>(&source);v&&bases[v->value])pinned[i]=1;}
}
ObjectRestPlan BuildObjectRests(const ValueProgram& g,const EvaluationPlan& p,TreeCheckBudget& b){
  ObjectRestPlan result;std::size_t count=0;for(const auto& op:g.ops){b.Step();count+=op.life==ValueLife::kLive&&std::holds_alternative<ValueObjectRest>(op.operation);}if(!count)return result;
  b.Array(result.entries,count);auto nonnull=NonNull(g,p,b);auto built=BuildEffectOrder(g,b.Remaining());if(const auto* error=std::get_if<ValueAvailabilityError>(&built))throw *error==ValueAvailabilityError::kWorkLimit?TreeStructureError::kWorkLimit:*error==ValueAvailabilityError::kResourceLimit?TreeStructureError::kResourceLimit:kProof;auto order=std::get<EffectOrder>(std::move(built));b.Admit(order.allocated_bytes,1);b.Step(order.work);
  std::vector<std::uint8_t> owned;b.Array(owned,g.ops.size());std::size_t next=0;
  std::vector<std::optional<UseId>> heads,links;b.Array(heads,g.values.size());b.Array(links,g.uses.size());
  for(std::uint32_t u=0;u<g.uses.size();++u){b.Step();const auto& use=g.uses[u];if(use.life!=ValueLife::kLive)continue;const auto input=Canonical(p,use.operand);const auto* value=std::get_if<ValueId>(&input);if(!value)continue;if(const auto* owner=std::get_if<OpId>(&use.owner);owner&&(p.operations[owner->value].kind==EvaluationKind::kAlias||p.operations[owner->value].kind==EvaluationKind::kElidedPure))continue;links[u]=heads[value->value];heads[value->value]=UseId{u};}

  std::vector<std::optional<ValueHandler>> handlers;b.Array(handlers,g.ops.size());
  std::vector<std::uint8_t> exits;b.Array(exits,g.ops.size());
  for(const auto& exit:g.exceptional_exits){b.Step();RequireTree(exit.op.value<exits.size()&&!exits[exit.op.value]++,kProof);handlers[exit.op.value]=exit.handler;}

  for(std::uint32_t i=0;i<g.ops.size();++i){b.Step();const auto& op=g.ops[i];const auto* rest=std::get_if<ValueObjectRest>(&op.operation);if(!rest||op.life!=ValueLife::kLive)continue;RequireTree(nonnull[i]==3&&exits[i],kProof);auto source=Input(g,p,{i},0);const auto* id=std::get_if<ValueId>(&source);RequireTree(id,kProof);auto& entry=result.entries[next++];entry.copy={i};entry.source=*id;
    const bool empty=rest->keys&&Special(g,p,Input(g,p,{i},1))==SpecialValue::kUndefined;b.Array(entry.fields,empty?0:rest->keys);OpId cursor{i};
    for(std::size_t k=entry.fields.size();k>0;--k){b.Step();const auto* previous=std::get_if<OpId>(&order.before[cursor.value]);RequireTree(previous&&!owned[previous->value],kProof);cursor=*previous;const auto& read=g.ops[cursor.value];RequireTree(read.result&&nonnull[cursor.value]==3&&Same(Input(g,p,cursor,0),source),kProof);auto key=Input(g,p,{i},static_cast<std::uint32_t>(k));
      if(const auto* named=std::get_if<ValueNamedRead>(&read.operation)){RequireTree(named->lookup==PropertyLookup::kObject,kProof);const auto* key_id=std::get_if<ValueId>(&key);RequireTree(key_id,kProof);const auto* def=std::get_if<OpResult>(&g.values[key_id->value].origin);RequireTree(def,kProof);const auto* string=std::get_if<ValueString>(&g.ops[def->op.value].operation);RequireTree(string&&string->kind==PoolTextKind::kString&&string->string.value==named->name.value,kProof);}
      else {const auto* computed=std::get_if<ValuePropertyRead>(&read.operation);RequireTree(computed&&computed->lookup==PropertyLookup::kObject&&SameKey(g,Input(g,p,cursor,1),key),kProof);const auto* key_id=std::get_if<ValueId>(&key);RequireTree(key_id&&g.values[key_id->value].domain.IsString(),kProof);}
      RequireTree(order.after[cursor.value].has_value()&&exits[cursor.value]&&handlers[cursor.value]==handlers[i],kProof);
      for(auto use=heads[std::get<ValueId>(Canonical(p,*read.result)).value];use;use=links[use->value]){b.Step();const auto& occurrence=g.uses[use->value];if(occurrence.life!=ValueLife::kLive)continue;const auto* consumer=std::get_if<OpId>(&occurrence.owner);RequireTree(consumer,kProof);if(p.operations[consumer->value].kind==EvaluationKind::kAlias||p.operations[consumer->value].kind==EvaluationKind::kElidedPure)continue;const auto& at=g.ops[consumer->value];RequireTree(order.Before(g,{i},at.block,at.order),kProof);}
      entry.fields[k-1]={cursor,key};owned[cursor.value]=1;
    }
  }
  return result;
}
bool VerifyObjectRests(const ValueProgram& g,const EvaluationPlan& p,const ObjectRestPlan& plan,TreeCheckBudget& b){auto actual=BuildObjectRests(g,p,b);if(actual.entries.size()!=plan.entries.size())return false;for(std::size_t i=0;i<actual.entries.size();++i){b.Step();const auto& a=actual.entries[i];const auto& c=plan.entries[i];if(a.copy!=c.copy||a.source!=c.source||a.fields.size()!=c.fields.size())return false;for(std::size_t k=0;k<a.fields.size();++k){b.Step();if(a.fields[k].read!=c.fields[k].read||!Same(a.fields[k].key,c.fields[k].key))return false;}}return true;}
}
