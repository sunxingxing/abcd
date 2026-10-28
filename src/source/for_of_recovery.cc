#include "recovery_flow.h"
#include "recovery_scratch.h"
#include "flow_dominance.h"
#include "completion_ports.h"
#include "iterator_continuation.h"
namespace abcd::source {
namespace {
using namespace analysis;
template<class T>void Need(const T& value){RequireTree(bool(value),TreeStructureError::kOwnership);}
void Arc(const RecoveryFlow& f,FlowPoint a,FlowPoint b,FlowCompletion k,TreeCheckBudget& budget){
  Need(a.value<f.events.size()&&b.value<f.events.size());bool found=false;
  for(auto e=f.events[a.value].outgoing;e;e=f.edges[*e].outgoing){budget.Step();const auto& edge=f.edges[*e];if(edge.completion==k){Need(!found&&edge.to==b);found=true;}}
  Need(found);
}
// Replayed for initial and every trial flow. Immutable original ports do not
// authorize a moved source action, a different native iterator or a new target.
void Close(const RecoveryFlow& f,const FlowIterationClose& c,TreeCheckBudget& b){
  for(const auto p:{c.entry,c.read,c.missing,c.call,c.check,c.success,c.getter_error,c.call_error,c.primitive,c.target,c.thrown})Need(p.value<f.events.size());
  Arc(f,c.entry,c.read,FlowCompletion::kNormal,b);Arc(f,c.read,c.missing,FlowCompletion::kNormal,b);Arc(f,c.read,c.getter_error,FlowCompletion::kThrow,b);
  Arc(f,c.missing,c.success,FlowCompletion::kTaken,b);Arc(f,c.missing,c.call,FlowCompletion::kNotTaken,b);
  Arc(f,c.call,c.check,FlowCompletion::kNormal,b);Arc(f,c.call,c.call_error,FlowCompletion::kThrow,b);
  Arc(f,c.check,c.success,FlowCompletion::kTaken,b);Arc(f,c.check,c.primitive,FlowCompletion::kNotTaken,b);
  Arc(f,c.success,c.target,c.completion,b);
  const auto original=c.pending==PendingCompletionKind::kThrow;
  for(const auto p:{c.getter_error,c.call_error,c.primitive})Arc(f,p,original?c.target:c.thrown,FlowCompletion::kThrow,b);
  for(std::size_t i=0;i<c.outcomes.size();++i){
    b.Step();const auto& outcome=c.outcomes[i];const auto kind=static_cast<CloseOutcomeKind>(i+1);
    const bool preserved=original||kind==CloseOutcomeKind::kSuccessObject||kind==CloseOutcomeKind::kMissingOrNullish;
    Need(outcome.outcome==kind&&outcome.completion==(preserved?c.pending:PendingCompletionKind::kThrow)&&outcome.target==(preserved?c.target:c.thrown));
    if(preserved)Need(outcome.payload==FlowClosePayload{FlowPendingPayload{c.entry}});
    else {const auto operation=kind==CloseOutcomeKind::kGetterThrow?c.read:kind==CloseOutcomeKind::kCallThrow?c.call:c.check;Need(outcome.payload==FlowClosePayload{FlowCloseErrorPayload{operation}});}
  }
  for(const auto p:{c.read,c.call})Need(!f.events[p.value].expression&&f.events[p.value].effects&&f.events[p.value].effects->exceptional==ExceptionalCompletion::kMayThrow&&f.events[p.value].effect==ExpressionEvaluation::kRequired);
}
}
void VerifyForOfRecovery(const FunctionTree& tree,RecoveryFlow& f,TreeCheckBudget& budget){
  if(!f.original||!f.original->iterations)return;
  const auto& raw=*f.original;const auto& native=*raw.iterations;
  Need(native.view.close_blocks.size()==raw.blocks.size()&&raw.phases.size()==raw.blocks.size()&&native.operations.size()==raw.operations.size()&&native.values.size()==raw.values.size()&&native.loops.size()==native.statements.size()&&native.view.loops.size()==native.loops.size());
  Need(raw.canonical_values.size()==raw.values.size()&&native.bindings.size()==native.loops.size()&&native.close_members.size()==raw.phases.size()&&native.binding_values.size()==raw.values.size());
  VerifyIteratorContinuationRecoveryFacts(raw,native,budget);
  if (native.view.continuations) {
    // Every original completion port is still checked below against actual
    // native actions. The added proof only pins the exhausted normal edge.
    for (const auto& c : native.view.continuations->continuations) {
      budget.Step(); Need(c.protocol.value < native.view.loops.size());
      const auto& placement = native.view.loops[c.protocol.value];
      Need(placement.loop == c.loop && placement.exhausted == c.edge);
    }
  }
  RecoveryScratch scratch(budget);
  std::pmr::vector<std::optional<std::uint32_t>> events(raw.operations.size(),{},&scratch),exits(raw.operations.size(),{},&scratch);
  std::pmr::vector<std::optional<IteratorProtocolId>> handlers(raw.blocks.size(),{},&scratch);
  std::size_t raw_nodes=0;
  for(const auto& phase:raw.phases){budget.Step();raw_nodes=std::max(raw_nodes,std::size_t{phase.input.value}+1);}
  std::pmr::vector<std::optional<WorkBlockId>> raw_blocks(raw_nodes,{},&scratch);
  for(std::uint32_t b=0;b<raw.phases.size();++b){budget.Step();const auto n=raw.phases[b].input.value;Need(!raw_blocks[n]);raw_blocks[n]=WorkBlockId{b};}
  const auto* certificate=native.view.completion_ports.get();
  std::pmr::vector<std::optional<StmtId>> enclosing(tree.statements.size(),{},&scratch);
  if(certificate || native.view.continuations) {
    struct Task {StmtId id;std::optional<StmtId> loop;};
    std::pmr::vector<Task> tasks(&scratch);tasks.push_back({tree.body,{}});
    std::pmr::vector<std::uint8_t> seen(tree.statements.size(),0,&scratch);
    for(std::size_t i=0;i<tasks.size();++i) {
      budget.Step();const auto [id,parent]=tasks[i];Need(id.value<seen.size()&&!seen[id.value]);seen[id.value]=1;
      enclosing[id.value]=parent;
      const auto add=[&](StmtId child,std::optional<StmtId> loop){tasks.push_back({child,loop});};
      std::visit([&](const auto& node){using T=std::decay_t<decltype(node)>;
        if constexpr(std::is_same_v<T,BlockStmt>)for(std::uint32_t n=0;n<node.child_count;++n){budget.Step();add(tree.children[node.first_child+n],parent);}
        else if constexpr(std::is_same_v<T,IfStmt>){add(node.consequent,parent);if(node.alternate)add(*node.alternate,parent);}
        else if constexpr(std::is_same_v<T,TryStmt>){add(node.body,parent);if(node.handler)add(node.handler->body,parent);if(node.finalizer)add(*node.finalizer,parent);}
        else if constexpr(std::is_same_v<T,ForOfStmt>)add(node.body,id);
        else if constexpr(std::is_same_v<T,LoopStmt>||std::is_same_v<T,ForInStmt>||std::is_same_v<T,SwitchStmt>||std::is_same_v<T,SwitchCaseStmt>)add(node.body,parent);
      },tree.statements[id.value].statement);
    }
  }
  std::pmr::vector<std::uint8_t> claimed(tree.statements.size(),0,&scratch);
  std::pmr::vector<LiveBinding> writes(&scratch);writes.reserve(native.loops.size());
  std::pmr::vector<std::optional<std::uint32_t>> close_entries(f.events.size(),{},&scratch);
  for(std::uint32_t c=0;c<f.iteration_closes.size();++c){budget.Step();const auto point=f.iteration_closes[c].entry;Need(point.value<close_entries.size()&&!close_entries[point.value]);close_entries[point.value]=c;}
  for(std::uint32_t e=0;e<raw.exceptional_exits.size();++e){budget.Step();const auto op=raw.exceptional_exits[e].op;Need(op.value<exits.size()&&!exits[op.value]);exits[op.value]=e;}
  for(std::uint32_t e=0;e<native.events.events.size();++e){budget.Step();const auto& event=native.events.events[e];Need(event.operation.value<events.size()&&!events[event.operation.value]);events[event.operation.value]=e;}
  for(std::uint32_t l=0;l<native.loops.size();++l){
    budget.Step();const auto& loop=native.loops[l];const auto statement=native.statements[l];
    Need(statement.value<tree.statements.size()&&statement.value<f.iterations.size()&&f.iterations[statement.value]);
    Need(!claimed[statement.value]);claimed[statement.value]=1;
    const auto* syntax=std::get_if<ForOfStmt>(&tree.statements[statement.value].statement);Need(syntax&&syntax->binding==ForOfBinding::kAssignment);
    const auto& binding=native.bindings[l];Need(syntax->target==binding.target&&syntax->iterable==binding.iterable&&syntax->body==binding.body&&tree.statements[statement.value].origin==binding.origin);
    auto& port=*f.iterations[statement.value];
    if(certificate || native.view.continuations) {
      const auto expected=native.view.loops[l].enclosing;
      Need(!expected || expected->value<native.statements.size());
      const auto parent=expected?std::optional{native.statements[expected->value]}:std::nullopt;
      Need(port.enclosing==parent && port.enclosing==enclosing[statement.value]);
    }
    Need(port.statement==statement);Need(!port.finalizer);Need(port.rhs==f.expressions[syntax->iterable.value]);
    Need(f.normal_exits[syntax->iterable.value]==port.events[0].before);
    Need(port.throw_close<f.iteration_closes.size());const auto& close=f.iteration_closes[port.throw_close];
    Need(close.loop==statement&&close.pending==PendingCompletionKind::kThrow&&close.target==port.thrown&&close.thrown==port.thrown);Close(f,close,budget);
    Need(f.statement_semantics[statement.value]&&f.statement_semantics[statement.value]->repeat_point==port.events[2].before);
    for(std::uint32_t kind=0;kind<port.events.size();++kind){
      budget.Step();const auto& p=port.events[kind];Need(p.kind==static_cast<ForOfEventKind>(kind)&&p.thrown==port.thrown);
      for(const auto point:{p.before,p.commit,p.thrown})Need(point.value<f.events.size());
      const auto op=kind==0?loop.acquisition:kind==1?loop.next:kind==2?loop.advance:kind==3?loop.check:kind==4?loop.done:kind==5?loop.decision:loop.value;
      Need(op.value<events.size()&&events[op.value]);const auto& event=native.events.events[*events[op.value]];
      Need(event.protocol==loop.protocol&&event.kind==p.kind&&event.before==raw.operations[op.value].block&&event.result==raw.operations[op.value].result&&event.exception_exit==exits[op.value]);
      Need(event.before.value<raw.phases.size()&&event.commit.value<raw.phases.size());
      Need(raw.phases[event.before.value].phase==ExceptionPhase::kBefore&&raw.phases[event.before.value].operation==op&&raw.phases[event.commit.value].phase==ExceptionPhase::kCommit&&raw.phases[event.commit.value].operation==op);
      Need(raw.phases[event.before.value].input==event.original_before&&raw.phases[event.commit.value].input==event.original_commit);
      if(event.result){const auto* result=std::get_if<OpResult>(&raw.values[event.result->value].origin);Need(result&&result->op==op&&result->commit==event.commit);}
      bool normal=false,raised=false;const auto& block=raw.blocks[event.before.value];
      for(std::uint32_t n=0;n<block.edge_count;++n){budget.Step();const auto& edge=raw.edges[block.first_edge.value+n];Need(edge.source==event.before);
        if(edge.kind==FlowEdgeKind::kException){Need(!raised&&event.exception_exit&&raw.exceptional_exits[*event.exception_exit].handler&&raw.exceptional_exits[*event.exception_exit].handler->entry==edge.target);raised=true;}
        else {Need(!normal&&edge.kind==FlowEdgeKind::kFallthrough&&edge.target==event.commit);normal=true;}}
      Need(normal&&raised==(event.exception_exit&&raw.exceptional_exits[*event.exception_exit].handler.has_value()));
      Arc(f,p.before,p.commit,FlowCompletion::kNormal,budget);
      if(kind!=5){Need(!f.events[p.before.value].expression&&f.events[p.before.value].effects&&f.events[p.before.value].effects->exceptional==ExceptionalCompletion::kMayThrow);Arc(f,p.before,p.thrown,FlowCompletion::kThrow,budget);}
      f.original_actions[op.value]=FlowOriginalAction{p.before,p.commit,{p.thrown,{}}};
      f.original_phases[event.before.value]=p.before;f.original_phases[event.commit.value]=p.commit;
      if(event.exception_exit){const auto id=*event.exception_exit;Need(id<f.original_exceptions.size());const auto& exit=raw.exceptional_exits[id];Need(exit.op==op&&exit.origin==event.origin);f.original_exceptions[id]=FlowIterationException{loop.protocol,id,p.before,p.thrown};}
    }
    for(std::uint32_t i=0;i<static_cast<std::uint32_t>(ForOfEventKind::kDoneDecision);++i)Arc(f,port.events[i].commit,port.events[i+1].before,FlowCompletion::kNormal,budget);
    Need(port.done==port.events[5].commit);Arc(f,port.done,port.exit,FlowCompletion::kTaken,budget);Arc(f,port.done,port.events[6].before,FlowCompletion::kNotTaken,budget);
    Arc(f,port.events[6].commit,port.write,FlowCompletion::kNormal,budget);Arc(f,port.write,port.commit,FlowCompletion::kNormal,budget);Arc(f,port.commit,port.body,FlowCompletion::kNormal,budget);Arc(f,port.write,close.entry,FlowCompletion::kThrow,budget);
    Need(f.events[port.commit.value].definition.has_value());const auto& definition=f.definitions[f.events[port.commit.value].definition->value];Need(definition.iteration==statement&&definition.binding==syntax->target&&!definition.value&&!definition.original&&!definition.input);
    writes.push_back({port.write,syntax->target});
    // Dynamic reference identity: cached next/receiver remain original values,
    // never source-variable reads that can be rebound by a body callback.
    const auto& call=raw.operations[loop.advance.value];bool callee=false,receiver=false;
    for(std::uint32_t u=call.first_use.value;u<call.first_use.value+call.use_count;++u){budget.Step();const auto& use=raw.uses[u];Need(use.life==ValueLife::kLive&&std::get_if<OpId>(&use.owner)&&std::get<OpId>(use.owner)==loop.advance);const auto* value=std::get_if<ValueId>(&use.operand);Need(value);
      if(use.role==OperandRole::kCallee){Need(!callee&&raw.canonical_values[value->value]==raw.canonical_values[loop.cached_next.value]);callee=true;}
      else if(use.role==OperandRole::kReceiver){Need(!receiver&&raw.canonical_values[value->value]==raw.canonical_values[loop.iterator.value]);receiver=true;}}
    Need(callee&&receiver);
  }
  for(std::uint32_t s=0;s<f.iterations.size();++s){budget.Step();Need(!f.iterations[s]||claimed[s]);}
  Need(native.events.ports==native.view.ports);
  for(const auto& close:f.iteration_closes){
    budget.Step();Need(close.loop.value<f.iterations.size()&&f.iterations[close.loop.value]&&claimed[close.loop.value]);const auto& ports=*f.iterations[close.loop.value];
    Need(close.thrown==ports.thrown);
    if(certificate && close.pending!=PendingCompletionKind::kThrow && close.target.value<close_entries.size() && close_entries[close.target.value]) {
      const auto& next=f.iteration_closes[*close_entries[close.target.value]];
      Need(ports.enclosing && next.loop==*ports.enclosing && next.pending==close.pending);
    } else switch(close.pending){
      case PendingCompletionKind::kThrow:Need(close.target==ports.thrown);break;
      case PendingCompletionKind::kReturn:Need(close.target==f.terminal);break;
      case PendingCompletionKind::kBreak:Need(close.target==ports.exit);break;
      case PendingCompletionKind::kContinue:Need(certificate && ports.enclosing && f.iterations[ports.enclosing->value] && close.target==f.iterations[ports.enclosing->value]->events[2].before);break;
      case PendingCompletionKind::kNormal:Need(false);break;
    }
    Close(f,close,budget);
  }
  // Resolve original completion cuts backwards through the acyclic port
  // graph. An unobserved Throw transport performs ZERO native close events;
  // this is checked from the independently replayed outcome, never from a
  // handler interval or from its being compiler-generated.
  std::pmr::vector<std::optional<FlowPoint>> routes(&scratch);
  const auto terminal=[&](const PendingCompletion& pending)->FlowPoint {
    Need(pending.kind==PendingCompletionKind::kThrow);
    if(std::holds_alternative<CompletionCaller>(pending.target))return f.terminal;
    const auto target=std::get<ExceptionNodeId>(pending.target);
    Need(target.value<raw_blocks.size() && raw_blocks[target.value]);
    const auto block=*raw_blocks[target.value];Need(!native.view.close_blocks[block.value]);
    const auto mapped=FindOriginalPhase(f,block,ExceptionPhase::kCatch);Need(mapped);return *mapped;
  };
  if(certificate) {
    routes.resize(certificate->qualified.size());
    Need(certificate->first_exit.size()==certificate->entries.size()+1 && certificate->order.size()==certificate->entries.size());
    std::pmr::vector<std::uint32_t> rank(certificate->entries.size(),0,&scratch),order(&scratch);
    for(std::uint32_t i=0;i<certificate->order.size();++i){budget.Step();Need(certificate->order[i]<rank.size());rank[certificate->order[i]]=i;}
    order.reserve(routes.size());for(std::uint32_t i=0;i<routes.size();++i)order.push_back(i);
    std::sort(order.begin(),order.end(),[&](auto x,auto y){budget.Step();return rank[certificate->qualified[x].entry]>rank[certificate->qualified[y].entry];});
    for(const auto id:order) {
      budget.Step();const auto& route=certificate->qualified[id];Need(route.entry<certificate->entries.size());
      const auto& entry=certificate->entries[route.entry];
      if(entry.original.pending.kind!=PendingCompletionKind::kThrow)continue;
      if(route.executes) {
        Need(entry.protocol.value<native.statements.size());const auto statement=native.statements[entry.protocol.value];
        Need(f.iterations[statement.value]);const auto close=f.iterations[statement.value]->throw_close;
        Need(close<f.iteration_closes.size());routes[id]=f.iteration_closes[close].entry;continue;
      }
      std::optional<FlowPoint> destination;
      Need(route.count && route.first<=certificate->transitions.size() && route.count<=certificate->transitions.size()-route.first);
      for(auto p=route.first;p<route.first+route.count;++p) {
        budget.Step();const auto& transition=certificate->transitions[p];Need(transition.exit<certificate->exits.size());
        const auto& exit=certificate->exits[transition.exit];Need(exit.entry==route.entry);
        Need(exit.original.outcome.kind==CloseOutcomeKind::kUnproved && exit.original.outcome.gap==CloseOutcomeGap::kNotObserved && exit.original.pending==entry.original.pending);
        std::optional<FlowPoint> target;
        if(transition.next){Need(*transition.next<routes.size());target=routes[*transition.next];Need(target);}
        else target=terminal(exit.original.pending);
        Need(!destination || destination==target);destination=target;
      }
      routes[id]=destination;
    }
  }
  // B's immutable entry-qualified ports must agree with the standard native
  // completion priority. A close-error identity is not the pending payload.
  for(const auto& port:native.events.ports){
    budget.Step();Need(port.protocol.value<native.loops.size()&&port.unit.value<native.view.closes.size()&&native.view.closes[port.unit.value]);
    const auto& original=*native.view.closes[port.unit.value];Need(original.protocol==port.protocol&&original.entry==port.entry&&original.pending==port.pending);
    const auto& out=port.outcome;Need(!ClosePriorityMismatch(port.pending,out));
    if(out.pending.kind==PendingCompletionKind::kThrow) {
      const auto statement=native.statements[port.protocol.value];
      const auto& loop=*f.iterations[statement.value];
      if(certificate) {
        const auto e=std::lower_bound(certificate->entries.begin(),certificate->entries.end(),original.entry,
            [&](const auto& entry,auto edge){budget.Step();return entry.original.edge<edge;});
        Need(e!=certificate->entries.end() && e->original.edge==original.entry && e->protocol==port.protocol && e->original.pending==port.pending);
        const auto index=static_cast<std::uint32_t>(e-certificate->entries.begin());
        const auto first=certificate->first_exit[index],last=certificate->first_exit[index+1];
        const auto p=std::lower_bound(certificate->exits.begin()+first,certificate->exits.begin()+last,out,
            [&](const auto& exit,const auto& value){budget.Step();return exit.original<value;});
        Need(p!=certificate->exits.begin()+last && p->original==out);
        Need(index<certificate->roots.size() && certificate->roots[index]);
        const auto root=*certificate->roots[index];Need(root<certificate->qualified.size());
        const auto& route=certificate->qualified[root];Need(route.entry==index && route.executes);
        const auto exit_id=static_cast<std::uint32_t>(p-certificate->exits.begin());
        const auto first_transition=certificate->transitions.begin()+route.first;
        const auto last_transition=first_transition+route.count;
        const auto transition=std::lower_bound(first_transition,last_transition,exit_id,
            [&](const auto& item,auto value){budget.Step();return item.exit<value;});
        if(transition==last_transition || transition->exit!=exit_id) {
          Need(out.outcome.kind==CloseOutcomeKind::kUnproved && out.outcome.gap==CloseOutcomeGap::kNotObserved);
          continue; // Certified false guard arm has no read/call/check event.
        }
        const auto target=transition->next?routes[*transition->next]:std::optional{terminal(out.pending)};
        Need(target && *target==loop.thrown);
      } else {
      if(const auto* target=std::get_if<ExceptionNodeId>(&out.pending.target)) {
        Need(target->value<raw_blocks.size() && raw_blocks[target->value]);
        const auto block=*raw_blocks[target->value];
        if(native.view.close_blocks[block.value]) {
          // A failed non-Throw close can enter this activation's already-
          // closed body handler. Its independent close certificate owns the
          // current-completion propagation; it is never a user catch.
          Need(native.close_members[block.value] && native.close_members[block.value]->protocol==port.protocol);
        } else {
          Need(raw.phases[block.value].phase==ExceptionPhase::kCatch);
          const auto handler=FindOriginalPhase(f,block,ExceptionPhase::kCatch);
          Need(handler && *handler==loop.thrown);
        }
      } else Need(loop.thrown==f.terminal);
      }
    }
    if(port.pending.kind==PendingCompletionKind::kThrow){
      Need(out.pending==port.pending);
      const auto statement=native.statements[port.protocol.value];const auto& c=f.iteration_closes[f.iterations[statement.value]->throw_close];
      for(const auto& outcome:c.outcomes){budget.Step();Need(outcome.completion==PendingCompletionKind::kThrow&&outcome.payload==FlowClosePayload{FlowPendingPayload{c.entry}});}
    }
    else switch(out.outcome.kind){
      case CloseOutcomeKind::kSuccessObject:case CloseOutcomeKind::kMissingOrNullish:Need(out.pending==port.pending);break;
      case CloseOutcomeKind::kGetterThrow:case CloseOutcomeKind::kCallThrow:Need(out.outcome.exception&&out.pending.kind==PendingCompletionKind::kThrow&&out.pending.payload==*out.outcome.exception);break;
      case CloseOutcomeKind::kPrimitiveResult:Need(out.pending.kind==PendingCompletionKind::kThrow);break;
      case CloseOutcomeKind::kUnproved:Need(false);
    }
  }
  std::pmr::vector<std::uint8_t> commits(raw.operations.size(),0,&scratch),roots(native.loops.size(),0,&scratch);
  std::pmr::vector<std::optional<IteratorProtocolId>> binding_values(raw.values.size(),{},&scratch);
  for(const auto& commit:native.events.commits){
    budget.Step();const auto& d=commit.definition;Need(d.protocol.value<native.loops.size()&&d.operation.value<raw.operations.size()&&d.value.value<raw.values.size()&&d.value.value<native.input_values.size());
    Need(!commits[d.operation.value]);commits[d.operation.value]=1;
    Need(!binding_values[d.value.value] || binding_values[d.value.value]==d.protocol);
    binding_values[d.value.value]=d.protocol;
    const auto& loop=native.loops[d.protocol.value];const auto& op=raw.operations[d.operation.value];
    const auto* result=std::get_if<OpResult>(&raw.values[d.value.value].origin);
    Need(op.result==d.value&&result&&result->op==d.operation&&result->commit==commit.commit&&commit.commit.value<raw.phases.size());
    Need(native.input_values[d.value.value].value==d.original.value&&raw.phases[commit.commit.value].input==commit.original_commit);
    Need(raw.canonical_values[d.value.value]==raw.canonical_values[loop.key.value]);
    const auto& view=native.view.loops[d.protocol.value];const auto& success=raw.edges[view.success.value];const auto& exhausted=raw.edges[view.exhausted.value];
    Need(commit.success==CompletionEdge{raw.phases[success.source.value].input,success.successor}&&commit.exhausted==CompletionEdge{raw.phases[exhausted.source.value].input,exhausted.successor});
    Need(success.source==exhausted.source&&view.success!=view.exhausted);
    if(d.operation==loop.value)roots[d.protocol.value]=1;
    f.original_phases[commit.commit.value]=FlowIterationPhase{d.protocol,commit.commit,FlowIterationPhaseKind::kBindingCommit};
  }
  for(const auto root:roots){budget.Step();Need(root);}
  for(std::size_t v=0;v<binding_values.size();++v){budget.Step();Need(binding_values[v]==native.binding_values[v]);}
  for(const auto& phi:native.phis){budget.Step();Need(phi.protocol.value<native.loops.size()&&phi.phi.value<raw.values.size()&&phi.input.value<raw.uses.size()&&phi.edge.value<raw.edges.size());
    const auto& use=raw.uses[phi.input.value];Need(use.life==ValueLife::kLive&&std::get_if<ValueId>(&use.owner)&&std::get<ValueId>(use.owner)==phi.phi&&std::get_if<ValueId>(&use.operand)&&std::get<ValueId>(use.operand)==phi.value&&use.incoming&&std::get_if<EdgeId>(&*use.incoming)&&std::get<EdgeId>(*use.incoming)==phi.edge);
  }
  // Retain close phases as typed protocol correspondence. No interior Catch
  // or VM result lends a fabricated source point or expression to a consumer.
  for(std::uint32_t b=0;b<raw.phases.size();++b){budget.Step();if(!native.view.close_blocks[b])continue;
    Need(native.close_members[b]);const auto owner=native.close_members[b]->protocol;Need(owner.value<native.loops.size());
    handlers[b]=owner;f.original_phases[b]=FlowIterationPhase{owner,WorkBlockId{b}};
  }
  for(std::uint32_t e=0;e<raw.exceptional_exits.size();++e){budget.Step();const auto& exit=raw.exceptional_exits[e];if(!exit.handler||!native.view.close_blocks[exit.handler->entry.value])continue;
    // A forwarded cleanup rethrow is owned by the protocol forwarding proof.
    if(!native.view.forwarding.operations.empty() && native.view.forwarding.operations[exit.op.value])continue;
    auto owner=handlers[exit.handler->entry.value];if(!owner&&native.operations[exit.op.value])owner=native.operations[exit.op.value]->protocol;
    Need(owner);const auto statement=native.statements[owner->value];const auto& p=*f.iterations[statement.value];const auto& close=f.iteration_closes[p.throw_close];
    const auto member=native.operations[exit.op.value];
    if(!member) {
      if(f.original_exceptions[e] && std::holds_alternative<FlowExcludedResourceFailure>(*f.original_exceptions[e])) {
        // Projection proved the cause (e.g. a body scope's environment
        // allocation) and the contract excludes it; no action is asserted.
      } else if(raw.operations[exit.op.value].number_identity) {
        const auto input=FindFlowOriginalNumberIdentity(f,exit.op);Need(input);
        f.original_exceptions[e]=FlowExcludedNumberFailure{input->expression,input->input,close.entry};
      } else if(raw.operations[exit.op.value].initialized_guard) {
        // The common guarded-read solver owns conditional TDZ exclusion.
        // Keep its value/provenance dependency; do not fabricate an action
        // for the erased VM guard or weaken the loop's exception context.
        const auto input=FindFlowOriginalGuard(f,exit.op);Need(input);
        Need(input->expression.value<f.actions.size() && f.actions[input->expression.value]);
        const auto& action=*f.actions[input->expression.value];
        Need(action.exceptions.thrown==close.entry && !action.exceptions.finalizer);
        f.original_exceptions[e]=FlowExcludedGuardFailure{input->expression,input->entry,close.entry};
      } else {
        auto action=f.original_actions[exit.op.value];
        if(FindOriginalActionRegion(raw,exit.op)) {
          const auto id=*raw.action_region_members[exit.op.value];
          Need(id.value<f.original_regions.size() && f.original_regions[id.value]);
          const auto& native=*f.original_regions[id.value];
          action=FlowOriginalAction{native.before,native.normal,native.exceptions};
        }
        Need(action && action->exceptions.thrown==close.entry && !action->exceptions.finalizer);
        f.original_exceptions[e]=FlowIterationException{*owner,e,action->before,close.entry};
      }
    }
    else if(!f.original_exceptions[e]) {
      const auto block=raw.operations[exit.op.value].block;Need(block.value<native.close_members.size()&&native.close_members[block.value]);
      const auto source_owner=native.close_members[block.value]->protocol;
      if(!certificate)Need(source_owner==owner);
      Need(source_owner.value<native.statements.size());const auto source_statement=native.statements[source_owner.value];
      const auto& source_port=*f.iterations[source_statement.value];const auto& source_close=f.iteration_closes[source_port.throw_close];
      std::optional<FlowPoint> before;auto thrown=source_close.thrown;auto kind=FlowIterationFailureKind::kPrivate;
      switch(native.close_members[block.value]->role){
        case CloseMemberRole::kReturnRead:before=source_close.read;thrown=source_close.getter_error;kind=FlowIterationFailureKind::kCloseRead;break;
        case CloseMemberRole::kReturnCall:before=source_close.call;thrown=source_close.call_error;kind=FlowIterationFailureKind::kCloseCall;break;
        case CloseMemberRole::kResultCheck:before=source_close.check;thrown=source_close.primitive;kind=FlowIterationFailureKind::kCloseCheck;break;
        case CloseMemberRole::kUnproved:Need(false);break;
        case CloseMemberRole::kScalar:case CloseMemberRole::kCatch:case CloseMemberRole::kCompletion:case CloseMemberRole::kComparison:case CloseMemberRole::kInfeasible:break;
      }
      f.original_exceptions[e]=FlowIterationException{source_owner,e,before,thrown,kind};
    }
  }
  struct CloseTail {FlowPoint target;StmtId loop;};
  std::pmr::vector<std::optional<CloseTail>> tails(f.iteration_closes.size(),{},&scratch);
  std::pmr::vector<std::uint8_t> tail_state(tails.size(),0,&scratch);
  std::pmr::vector<std::uint32_t> tail_path(&scratch);
  if(certificate)for(std::uint32_t start=0;start<tails.size();++start) {
    if(tails[start])continue;auto current=start;tail_path.clear();
    for(;;) {
      budget.Step();Need(current<tails.size() && tail_state[current]!=1);
      if(tails[current])break;
      tail_state[current]=1;tail_path.push_back(current);const auto& close=f.iteration_closes[current];
      const auto next=close.target.value<close_entries.size()?close_entries[close.target.value]:std::nullopt;
      if(!next){tails[current]=CloseTail{close.target,close.loop};break;}
      Need(f.iteration_closes[*next].pending==close.pending);current=*next;
    }
    for(auto i=tail_path.size();i!=0;--i) {
      budget.Step();const auto at=tail_path[i-1];if(!tails[at]) {
        const auto next=close_entries[f.iteration_closes[at].target.value];Need(next && tails[*next]);tails[at]=tails[*next];
      }
      tail_state[at]=2;
    }
  }
  for(const auto& exit:native.exits){
    budget.Step();Need(exit.loop<native.statements.size()&&exit.statement.value<tree.statements.size());
    const auto statement=native.statements[exit.loop];const auto& p=*f.iterations[statement.value];const auto& node=tree.statements[exit.statement.value];Need(node.origin==exit.origin);
    Need(f.statement_semantics[exit.statement.value]&&f.statement_semantics[exit.statement.value]->completion==FlowStatementCompletion::kJump);
    if(exit.kind==LoopTransferKind::kContinue){const auto* jump=std::get_if<ContinueStmt>(&node.statement);Need(jump&&jump->target==statement);Arc(f,f.statements[exit.statement.value],p.events[2].before,FlowCompletion::kJump,budget);}
    else {
      const auto* jump=std::get_if<BreakStmt>(&node.statement);Need(jump&&jump->target==statement);
      std::optional<FlowPoint> entry;for(auto e=f.events[f.statements[exit.statement.value].value].outgoing;e;e=f.edges[*e].outgoing){budget.Step();if(f.edges[*e].completion==FlowCompletion::kJump){Need(!entry);entry=f.edges[*e].to;}}
      Need(entry&&entry->value<close_entries.size()&&close_entries[entry->value]);const auto& c=f.iteration_closes[*close_entries[entry->value]];
      if(certificate) {
        Need(enclosing[exit.statement.value] && c.loop==*enclosing[exit.statement.value] && c.pending==PendingCompletionKind::kBreak);
        const auto& tail=tails[*close_entries[entry->value]];Need(tail && tail->loop==statement && tail->target==p.exit);
      } else Need(c.loop==statement&&c.pending==PendingCompletionKind::kBreak&&c.target==p.exit&&c.thrown==p.thrown);
    }
  }
  // The binding is available before PutValue, not merely after a successful
  // assignment. Proven initialization also closes the possible TDZ failure.
  auto result=AnalyzeFlowInitializations(f,writes,budget.Remaining());if(const auto* error=std::get_if<TreeStructureError>(&result))throw *error;
  const auto& initialized=std::get<FlowInitializations>(result);budget.Admit(initialized.allocated_bytes,1);budget.Step(initialized.work);
  for(const auto w:writes){budget.Step();Need(initialized.Completed(w.binding,w.point));}
  for(const auto& input:native.inputs){budget.Step();Need(input.protocol.value<native.statements.size()&&input.use.value<raw.uses.size()&&input.expression.value<f.actions.size());const auto statement=native.statements[input.protocol.value];const auto& syntax=std::get<ForOfStmt>(tree.statements[statement.value].statement);const auto& p=*f.iterations[statement.value];const auto& a=f.actions[input.expression.value];Need(a&&f.events[a->before.value].kind==FlowEventKind::kRead&&f.events[a->before.value].read==syntax.target);
    if(initialized.order.tree.nodes[a->before.value].begin)Need(initialized.order.Dominates(p.commit,a->before));
  }
}
} // namespace abcd::source
