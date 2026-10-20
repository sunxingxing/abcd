#include "recovery_flow.h"
#include "recovery_scratch.h"
#include <algorithm>
namespace abcd::source {
namespace {
using namespace analysis;
template<class T>void Need(const T& v){RequireTree(bool(v),TreeStructureError::kOwnership);}
}
// Same universal forwarding proof as for-in, with every original throw entry
// retained (ordinary RHS/body actions as well as native iterator events).
void VerifyProtocolForwardingRecovery(const FunctionTree&,RecoveryFlow& flow,TreeCheckBudget& budget){
  if(!flow.original || !flow.original->iterations)return;
  const auto& raw=*flow.original;const auto& f=raw.iterations->view.forwarding;
  if(f.regions.empty())return;
  Need(f.blocks.size()==raw.blocks.size() && f.operations.size()==raw.operations.size() && f.exits.size()==raw.exceptional_exits.size());
  RecoveryScratch memory(budget);
  std::pmr::vector<std::optional<std::uint32_t>> exits(raw.operations.size(),{},&memory);
  std::pmr::vector<std::optional<std::uint32_t>> enumerations(raw.operations.size(),{},&memory);
  for (std::uint32_t i = 0; i < raw.enumerations.size(); ++i) {
    budget.Step(); const auto& loop = raw.enumerations[i];
    const auto* route = std::get_if<ForwardedExceptionRoute>(&loop.acquisition);
    if (!route || route->owner != ForwardingOwner::kProtocolProjection) continue;
    const auto operation = loop.original.acquisition.operation;
    Need(operation.value < enumerations.size() && !enumerations[operation.value]);
    enumerations[operation.value] = i;
  }
  std::pmr::vector<std::uint32_t> pending(raw.blocks.size(),0,&memory),offset(raw.blocks.size()+1,0,&memory);
  std::pmr::vector<WorkBlockId> pred(raw.edges.size(),{},&memory),queue(&memory);queue.reserve(raw.blocks.size());
  std::pmr::vector<std::uint8_t> terminal(raw.blocks.size(),0,&memory),good(raw.blocks.size(),0,&memory);
  Need(f.regions.size()==raw.exception_regions.size());
  std::size_t instructions=0;for(const auto& phase:raw.phases){budget.Step();instructions=std::max(instructions,std::size_t{phase.instruction.value}+1);}
  std::pmr::vector<std::uint32_t> overlap(instructions+1,0,&memory);
  for(std::size_t b=0;b<raw.phases.size();++b){budget.Step();if(raw.iterations->view.close_blocks[b])++overlap[raw.phases[b].instruction.value+1];}
  for(std::size_t i=1;i<overlap.size();++i){budget.Step();overlap[i]+=overlap[i-1];}
  for(std::size_t r=0;r<f.regions.size();++r){budget.Step();if(!f.regions[r])continue;for(const auto range:{raw.exception_regions[r].body,raw.exception_regions[r].handler}){Need(range.begin.value<=range.end.value && range.end.value<overlap.size() && overlap[range.begin.value]==overlap[range.end.value]);}}
  for(std::uint32_t e=0;e<raw.exceptional_exits.size();++e){budget.Step();const auto o=raw.exceptional_exits[e].op;Need(o.value<exits.size()&&!exits[o.value]);exits[o.value]=e;}
  for(std::uint32_t b=0;b<raw.blocks.size();++b){
    budget.Step();if(!f.blocks[b])continue;const auto r=*f.blocks[b];Need(r.value<f.regions.size() && f.regions[r.value] && !raw.iterations->view.close_blocks[b]);
    const auto& route=*f.regions[r.value];const auto& block=raw.blocks[b];
    for(std::uint32_t n=0;n<block.op_count;++n){budget.Step();const auto o=block.first_op.value+n;const auto& op=raw.operations[o];if(op.life!=ValueLife::kLive)continue;
      Need(f.operations[o]==r && !terminal[b] && op.normal==LanguageEffect::kNone && op.before_throw==LanguageEffect::kNone);
      if(op.forwarding_throw){Need(op.use_count==1 && op.exceptional==ExceptionalCompletion::kAlwaysThrow && exits[o]);const auto& use=raw.uses[op.first_use.value];const auto* value=std::get_if<ValueId>(&use.operand);
        Need(value && use.role==OperandRole::kThrownValue && raw.canonical_values[value->value]==route.payload && raw.exceptional_exits[*exits[o]].handler==route.destination);terminal[b]=1;
      }else Need(op.exceptional==ExceptionalCompletion::kImpossible);
    }
  }
  for(std::size_t o=0;o<raw.operations.size();++o){budget.Step();Need(raw.operations[o].block.value<f.blocks.size() && f.operations[o]==f.blocks[raw.operations[o].block.value]);}
  std::pmr::vector<std::uint8_t> incoming(raw.exceptional_exits.size(),0,&memory);
  for(const auto& edge:raw.edges){budget.Step();const auto a=f.blocks[edge.source.value],b=f.blocks[edge.target.value];if(!a&&!b)continue;
    if(edge.kind==FlowEdgeKind::kException){
      if(a && !b) {
        Need(terminal[edge.source.value] && f.regions[a->value]->destination && f.regions[a->value]->destination->entry==edge.target);
        continue; // Original same-payload terminal dispatch to an ordinary catch.
      }
      Need(b);const auto o=raw.phases[edge.source.value].operation;Need(o && exits[o->value] && f.exits[*exits[o->value]]);const auto& exit=raw.exceptional_exits[*exits[o->value]];Need(exit.handler && exit.handler->entry==edge.target && !incoming[*exits[o->value]]);incoming[*exits[o->value]]=1;continue;}
    Need(a && b && !terminal[edge.source.value] && f.regions[a->value]==f.regions[b->value]);++pending[edge.source.value];++offset[edge.target.value+1];
  }
  for(std::size_t e=0;e<f.exits.size();++e){budget.Step();Need(!f.exits[e] || incoming[e]);}
  for(std::size_t i=1;i<offset.size();++i){budget.Step();offset[i]+=offset[i-1];}
  auto cursor=std::pmr::vector<std::uint32_t>(offset,&memory);
  for(const auto& e:raw.edges){budget.Step();if(e.kind!=FlowEdgeKind::kException && f.blocks[e.source.value])pred[cursor[e.target.value]++]=e.source;}
  for(std::uint32_t b=0;b<raw.blocks.size();++b){budget.Step();if(f.blocks[b] && !pending[b]){Need(terminal[b]);good[b]=1;queue.push_back({b});}}
  for(std::size_t i=0;i<queue.size();++i)for(auto p=offset[queue[i].value];p<offset[queue[i].value+1];++p){budget.Step();auto b=pred[p].value;Need(pending[b]);if(!--pending[b]){good[b]=1;queue.push_back({b});}}
  // Resolve the enclosing transparent routes independently. Each rethrow keeps
  // its incoming payload even though the next Catch has a different SSA id.
  std::pmr::vector<std::optional<TryId>> handlers(raw.blocks.size(),{},&memory);
  for(std::uint32_t e=0;e<raw.exceptional_exits.size();++e){budget.Step();if(!f.exits[e])continue;const auto r=*f.exits[e];Need(r.value<f.regions.size() && f.regions[r.value]);const auto& exit=raw.exceptional_exits[e];Need(exit.handler);const auto h=exit.handler->entry;Need(h.value<f.blocks.size() && f.blocks[h.value] && f.regions[f.blocks[h.value]->value]==f.regions[r.value]);
    Need(f.regions[r.value]->payload.value<raw.values.size());const auto& payload=raw.values[f.regions[r.value]->payload.value];const auto* caught=std::get_if<CatchDefinition>(&payload.origin);Need(caught && caught->block==h);handlers[h.value]=r;
  }
  std::pmr::vector<std::optional<FlowPoint>> ordinary(raw.blocks.size(),{},&memory),targets(f.regions.size(),{},&memory);
  for(const auto& c:raw.catches){budget.Step();if(c && !f.blocks[c->handler.value]){
    Need(c->handler.value<ordinary.size());const auto point=FindOriginalPhase(flow,c->handler,ExceptionPhase::kCatch);Need(point);ordinary[c->handler.value]=*point;
  }}
  std::pmr::vector<std::uint32_t> degrees(f.regions.size(),0,&memory),parents(f.regions.size()+1,0,&memory),children(f.regions.size(),0,&memory),ready(&memory);ready.reserve(f.regions.size());
  for(std::uint32_t r=0;r<f.regions.size();++r){budget.Step();if(!f.regions[r])continue;
    if(const auto d=f.regions[r]->destination){Need(d->entry.value<handlers.size());
      if(handlers[d->entry.value]){degrees[r]=1;++parents[handlers[d->entry.value]->value+1];}
      else {Need(ordinary[d->entry.value]);targets[r]=ordinary[d->entry.value];ready.push_back(r);}
    }else {targets[r]=flow.terminal;ready.push_back(r);}}

  for(std::size_t r=1;r<parents.size();++r){budget.Step();parents[r]+=parents[r-1];}
  auto insert=std::pmr::vector<std::uint32_t>(parents,&memory);
  for(std::uint32_t r=0;r<f.regions.size();++r){budget.Step();if(f.regions[r] && f.regions[r]->destination && handlers[f.regions[r]->destination->entry.value])children[insert[handlers[f.regions[r]->destination->entry.value]->value]++]=r;}
  for(std::size_t i=0;i<ready.size();++i)for(auto p=parents[ready[i]];p<parents[ready[i]+1];++p){budget.Step();degrees[children[p]]=0;targets[children[p]]=targets[ready[i]];ready.push_back(children[p]);}
  for(auto d:degrees){budget.Step();Need(!d);}
  for(std::uint32_t b=0;b<raw.blocks.size();++b){budget.Step();if(!f.blocks[b])continue;Need(good[b]);flow.original_phases[b]=FlowForwardedTryPhase{*f.blocks[b],WorkBlockId{b}};if(raw.phases[b].phase==ExceptionPhase::kCatch)flow.original_handlers[b]=FlowOriginalRouteCoverage::kComplete;}
  for(std::uint32_t e=0;e<raw.exceptional_exits.size();++e){budget.Step();const auto& exit=raw.exceptional_exits[e];
    if(f.operations[exit.op.value]){const auto r=*f.operations[exit.op.value];Need(raw.operations[exit.op.value].forwarding_throw);flow.original_exceptions[e]=FlowForwardedTryDispatch{e,r};}
    else if(f.exits[e]) {
      const auto r=*f.exits[e];Need(exit.handler && targets[r.value]);
      FlowPoint before;
      if (flow.original_actions[exit.op.value]) {
        const auto& a=*flow.original_actions[exit.op.value];
        Need(a.exceptions.thrown==*targets[r.value] && !a.exceptions.finalizer);before=a.before;
      } else {
        // A native for-in acquisition has an event port, not a fabricated
        // ExprId/action. Its shared route was checked by ForInRecovery above;
        // this proof still rechecks every original handler edge and payload.
        Need(enumerations[exit.op.value]);
        const auto& loop=raw.enumerations[*enumerations[exit.op.value]];
        const auto& route=std::get<ForwardedExceptionRoute>(loop.acquisition);
        Need(route.exit.value==e && route.handler==exit.handler->entry &&
            route.payload==f.regions[r.value]->payload && route.destination==f.regions[r.value]->destination &&
            loop.statement.value<flow.enumerations.size() && flow.enumerations[loop.statement.value]);
        const auto& ports=*flow.enumerations[loop.statement.value];
        Need(ports.thrown==*targets[r.value]);before=ports.acquire_before;
      }
      flow.original_exceptions[e]=FlowForwardedException{before,*targets[r.value],ForInExceptionExitId{e},exit.handler->entry,f.regions[r.value]->payload};
    }
  }
}
}
