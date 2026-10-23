#include "recovery_flow.h"
#include "flow_dominance.h"
#include "recovery_scratch.h"
#include <algorithm>
namespace abcd::source {
namespace {
using namespace analysis;
using Error = TreeStructureError;
void Need(bool ok) { RequireTree(ok, Error::kOwnership); }
void Arc(const RecoveryFlow& flow, FlowPoint from, FlowPoint to,
         FlowCompletion completion, TreeCheckBudget& budget) {
  Need(from.value < flow.events.size() && to.value < flow.events.size());
  bool found = false;
  for (auto e = flow.events[from.value].outgoing; e; e = flow.edges[*e].outgoing) {
    budget.Step(); const auto& edge = flow.edges[*e];
    if (edge.completion == completion) { Need(!found && edge.to == to); found = true; }
  }
  Need(found);
}
void RawEvent(const OriginalFlow& raw, const ForInEvent& event,
              EnumerationAction action, TreeCheckBudget& budget) {
  budget.Step(); Need(event.operation.value < raw.operations.size() && event.result.value < raw.values.size() &&
      event.before.value < raw.phases.size() && event.commit.value < raw.phases.size() &&
      event.exceptional_exit.value < raw.exceptional_exits.size());
  const auto& op = raw.operations[event.operation.value];
  Need(op.enumeration == action && op.result == event.result && op.block == event.before && op.life == ValueLife::kLive);
  const auto* result = std::get_if<OpResult>(&raw.values[event.result.value].origin);
  Need(result && result->op == event.operation && result->commit == event.commit);
  const auto& before = raw.phases[event.before.value]; const auto& commit = raw.phases[event.commit.value];
  Need(before.phase == ExceptionPhase::kBefore && before.operation == event.operation &&
      commit.phase == ExceptionPhase::kCommit && commit.operation == event.operation);
  const auto& exit = raw.exceptional_exits[event.exceptional_exit.value];
  Need(exit.op == event.operation && exit.origin == op.origin && exit.before_throw == op.before_throw &&
      op.exceptional == ExceptionalCompletion::kMayThrow);
  const auto& block = raw.blocks[event.before.value]; bool normal = false, raised = false;
  for (std::uint32_t i = 0; i < block.edge_count; ++i) {
    budget.Step(); const auto& edge = raw.edges[block.first_edge.value+i];
    if (edge.kind == FlowEdgeKind::kException) {
      Need(!raised && exit.handler && edge.target == exit.handler->entry); raised = true;
    } else { Need(!normal && edge.kind == FlowEdgeKind::kFallthrough && edge.target == event.commit); normal = true; }
  }
  Need(normal && raised == exit.handler.has_value());
}
}
// Recomputed on every proposal. OriginalFlow is the immutable checked input;
// no source expression stands in for the enumerator or exhausted next-result.
void VerifyForInRecovery(const FunctionTree& tree, RecoveryFlow& flow, TreeCheckBudget& budget) {
  if (!flow.original || flow.original->enumerations.empty()) return;
  const auto& raw = *flow.original;
  Need(raw.enumeration_operations.size() == raw.operations.size() && raw.enumeration_values.size() == raw.values.size() &&
      raw.canonical_values.size() == raw.values.size() && raw.forwarded_blocks.size() == raw.blocks.size() &&
      raw.forwarded_operations.size() == raw.operations.size());
  RecoveryScratch memory(budget);
  std::pmr::vector<std::optional<std::uint32_t>> acquire(raw.operations.size(), {}, &memory);
  std::pmr::vector<LiveBinding> writes(&memory); writes.reserve(raw.enumerations.size());
  std::pmr::vector<std::optional<std::uint32_t>> exits(raw.operations.size(), {}, &memory);
  for (std::uint32_t e=0;e<raw.exceptional_exits.size();++e) {
    budget.Step(); const auto op=raw.exceptional_exits[e].op; Need(op.value<exits.size()&&!exits[op.value]); exits[op.value]=e;
  }
  for (std::uint32_t i=0;i<raw.enumerations.size();++i) {
    budget.Step(); const auto& original=raw.enumerations[i];
    Need(original.statement.value<tree.statements.size() && original.statement.value<flow.enumerations.size() && flow.enumerations[original.statement.value]);
    const auto* syntax=std::get_if<ForInStmt>(&tree.statements[original.statement.value].statement);
    Need(syntax && syntax->binding==ForInBinding::kAssignment && syntax->object==original.object && syntax->key==original.key);
    const auto& p=*flow.enumerations[original.statement.value]; const auto& events=original.original;
    for (const auto point : {p.acquire_before,p.acquire_commit,p.next_before,p.next_commit,
                            p.done,p.key_write,p.key_commit,p.body,p.exit,p.thrown})
      Need(point.value < flow.events.size());
    Need(flow.events[p.acquire_before.value].kind == FlowEventKind::kEnumerationAcquire &&
         flow.events[p.next_before.value].kind == FlowEventKind::kEnumerationNext &&
         flow.events[p.key_write.value].kind == FlowEventKind::kEnumerationBind);
    for (const auto point : {p.acquire_before,p.next_before,p.key_write}) {
      const auto& event = flow.events[point.value];
      Need(event.effects && event.effects->exceptional == ExceptionalCompletion::kMayThrow && !event.definition &&
           event.effect == ExpressionEvaluation::kRequired);
      const auto expected = point==p.key_write ?
          SourceEffects{LanguageEffect::kLexicalWrite,LanguageEffect::kNone,ExceptionalCompletion::kMayThrow} : CallbackEffects();
      Need(event.effects->normal==expected.normal && event.effects->before_throw==expected.before_throw);
    }
    for (const auto point : {p.acquire_commit,p.next_commit,p.done}) {
      const auto& event = flow.events[point.value];
      Need(event.kind == FlowEventKind::kControl && !event.definition && !event.expression);
    }
    Need(p.statement==original.statement && !p.finalizer && p.rhs==flow.expressions[syntax->object.value] &&
        flow.normal_exits[syntax->object.value]==p.acquire_before);
    RawEvent(raw,events.acquisition,EnumerationAction::kAcquire,budget);
    RawEvent(raw,events.advance,EnumerationAction::kNext,budget);
    const auto& next=raw.operations[events.advance.operation.value];
    Need(next.use_count==1 && next.first_use.value<raw.uses.size());
    const auto* enumerator=std::get_if<ValueId>(&raw.uses[next.first_use.value].operand);
    Need(enumerator && raw.canonical_values[enumerator->value]==raw.canonical_values[events.acquisition.result.value]);
    Need(events.key.next_result==events.advance.result && events.key.success!=events.key.exhausted &&
        events.key.success.value<raw.edges.size() && events.key.exhausted.value<raw.edges.size());
    const auto& success=raw.edges[events.key.success.value]; const auto& done=raw.edges[events.key.exhausted.value];
    Need(success.source==done.source && success.kind!=done.kind && success.source.value<raw.phases.size() &&
        raw.phases[success.source.value].operation==events.key.decision && raw.phases[success.source.value].phase==ExceptionPhase::kCommit);
    // Original TreeFlow establishes which original edge is exhausted. Preserve
    // that edge identity; do not infer it from a raw Boolean's numeric encoding.
    Need(original.success==events.key.success && original.exhausted==events.key.exhausted);
    Arc(flow,p.acquire_before,p.acquire_commit,FlowCompletion::kNormal,budget);
    Arc(flow,p.acquire_commit,p.next_before,FlowCompletion::kNormal,budget);
    Arc(flow,p.next_before,p.next_commit,FlowCompletion::kNormal,budget);
    Arc(flow,p.next_commit,p.done,FlowCompletion::kNormal,budget);
    Arc(flow,p.done,p.exit,FlowCompletion::kTaken,budget);
    Arc(flow,p.done,p.key_write,FlowCompletion::kNotTaken,budget);
    Arc(flow,p.key_write,p.key_commit,FlowCompletion::kNormal,budget);
    Arc(flow,p.key_commit,p.body,FlowCompletion::kNormal,budget);
    for(const auto event:{p.acquire_before,p.next_before,p.key_write}) Arc(flow,event,p.thrown,FlowCompletion::kThrow,budget);
    Need(flow.statement_semantics[original.statement.value]->repeat_point==p.next_before);
    const auto definition=flow.events[p.key_commit.value].definition;
    Need(definition && definition->value<flow.definitions.size());
    const auto& key=flow.definitions[definition->value];
    Need(key.binding==original.key && key.enumeration && key.enumeration->statement==original.statement && key.enumeration->original==events.key && key.point==p.key_commit &&
        !key.value && !key.original && !key.input);
    writes.push_back({p.key_write,key.binding}); acquire[events.acquisition.operation.value]=i;
    const auto& raised=raw.exceptional_exits[events.acquisition.exceptional_exit.value];
    if(const auto* forwarded=std::get_if<ForwardedExceptionRoute>(&original.acquisition)) {
      Need(forwarded->exit==events.acquisition.exceptional_exit && raised.handler && raised.handler->entry==forwarded->handler &&
          !forwarded->destination && p.thrown==flow.terminal);
      switch (forwarded->owner) {
        case ForwardingOwner::kEnumeration:
          Need(forwarded->handler.value < raw.forwarded_blocks.size() && raw.forwarded_blocks[forwarded->handler.value]);
          break;
        case ForwardingOwner::kProtocolProjection: {
          Need(raw.iterations.has_value());
          const auto& projection = raw.iterations->view.forwarding;
          Need(forwarded->handler.value < projection.blocks.size() && projection.blocks[forwarded->handler.value]);
          const auto region = *projection.blocks[forwarded->handler.value];
          Need(region.value < projection.regions.size() && projection.regions[region.value] &&
              projection.regions[region.value]->payload == forwarded->payload &&
              projection.regions[region.value]->destination == forwarded->destination &&
              forwarded->exit.value < projection.exits.size() && projection.exits[forwarded->exit.value] == region &&
              !raw.forwarded_blocks[forwarded->handler.value]);
          break;
        }
        default: Need(false);
      }
      Need(forwarded->payload.value < raw.values.size());
      const auto& payload=raw.values[forwarded->payload.value];
      const auto* caught=std::get_if<CatchDefinition>(&payload.origin);
      Need(caught && caught->block==forwarded->handler);
      flow.original_exceptions[forwarded->exit.value]=FlowForwardedException{p.acquire_before,p.thrown,forwarded->exit,forwarded->handler,forwarded->payload};
    } else {
      Need(std::get<PhysicalCatch>(original.acquisition).exit==events.acquisition.exceptional_exit);
      Need(flow.original_exceptions[events.acquisition.exceptional_exit.value].has_value());
    }
    const auto& next_failure=flow.original_exceptions[events.advance.exceptional_exit.value];
    Need(next_failure.has_value());
    const auto* next_route=std::get_if<FlowOriginalException>(&*next_failure);
    Need(next_route && next_route->before==p.next_before && next_route->thrown==p.thrown);
  }
  for(const auto& exit:raw.enumeration_exits) {
    budget.Step();Need(exit.loop<raw.enumerations.size() && exit.statement.value<tree.statements.size());
    const auto& original=raw.enumerations[exit.loop];const auto& p=*flow.enumerations[original.statement.value];
    const auto& node=tree.statements[exit.statement.value];Need(node.origin==exit.origin);
    if(exit.kind==LoopTransferKind::kContinue) {
      const auto* jump=std::get_if<ContinueStmt>(&node.statement);Need(jump&&jump->target==original.statement);
    } else {const auto* jump=std::get_if<BreakStmt>(&node.statement);Need(jump&&jump->target==original.statement);}
    Arc(flow,flow.statements[exit.statement.value],exit.kind==LoopTransferKind::kContinue?p.next_before:p.exit,FlowCompletion::kJump,budget);
  }
  // Universal termination and same-payload forwarding: reverse Kahn over the
  // certified subgraph, checking raw effects and EVERY incoming entry anew.
  std::pmr::vector<std::uint32_t> pending(raw.blocks.size(),0,&memory),offset(raw.blocks.size()+1,0,&memory);
  std::pmr::vector<WorkBlockId> preds(raw.edges.size(),{},&memory),queue(&memory);queue.reserve(raw.blocks.size());
  std::pmr::vector<std::uint8_t> terminal(raw.blocks.size(),0,&memory),good(raw.blocks.size(),0,&memory);
  for(std::uint32_t b=0;b<raw.blocks.size();++b) {
    budget.Step(); if(!raw.forwarded_blocks[b])continue;
    const auto l=*raw.forwarded_blocks[b]; Need(l<raw.enumerations.size());
    const auto* route=std::get_if<ForwardedExceptionRoute>(&raw.enumerations[l].acquisition); Need(route);
    const auto& block=raw.blocks[b];
    for(std::uint32_t j=0;j<block.op_count;++j) {
      budget.Step(); const auto id=block.first_op.value+j; const auto& op=raw.operations[id];
      if(op.life!=ValueLife::kLive)continue;
      Need(raw.forwarded_operations[id]==l && !terminal[b]);
      Need((op.normal==LanguageEffect::kNone || op.scope_pop) && op.before_throw==LanguageEffect::kNone);
      if(op.forwarding_throw) {
        Need(op.exceptional==ExceptionalCompletion::kAlwaysThrow && op.use_count==1 && exits[id]);
        const auto& use=raw.uses[op.first_use.value]; const auto* v=std::get_if<ValueId>(&use.operand);
        Need(use.role==OperandRole::kThrownValue && v && raw.canonical_values[v->value]==route->payload &&
            raw.exceptional_exits[*exits[id]].handler==route->destination);
        terminal[b]=1;
      } else Need(op.exceptional==ExceptionalCompletion::kImpossible);
    }
  }
  for(const auto& edge:raw.edges) {
    budget.Step(); if(!raw.forwarded_blocks[edge.target.value] && !raw.forwarded_blocks[edge.source.value])continue;
    if(!raw.forwarded_blocks[edge.source.value]) {
      Need(edge.kind==FlowEdgeKind::kException);
      const auto op=raw.phases[edge.source.value].operation; Need(op.has_value());
      if(acquire[op->value]) {
        const auto& route=std::get<ForwardedExceptionRoute>(raw.enumerations[*acquire[op->value]].acquisition);
        Need(route.handler==edge.target); continue;
      }
      // Another operation protected by the same transparent region (the RHS
      // read). The forward re-raises the same payload to its destination (an
      // ordinary catch) or the caller, so the operation's own source action
      // must already throw exactly there, without finalizer.
      const auto l=*raw.forwarded_blocks[edge.target.value]; Need(l<raw.enumerations.size());
      const auto& route=std::get<ForwardedExceptionRoute>(raw.enumerations[l].acquisition);
      Need(route.handler==edge.target && exits[op->value]);
      std::optional<FlowPoint> target=flow.terminal;
      if(route.destination) target=FindOriginalPhase(flow,route.destination->entry,ExceptionPhase::kCatch);
      Need(target.has_value());
      const auto& action=flow.original_actions[op->value];
      Need(action && action->exceptions.thrown==*target && !action->exceptions.finalizer);
      flow.original_exceptions[*exits[op->value]]=FlowForwardedException{action->before,*target,{*exits[op->value]},route.handler,route.payload};
      continue;
    }
    Need(edge.kind!=FlowEdgeKind::kException && raw.forwarded_blocks[edge.target.value] && !terminal[edge.source.value]);
    Need(raw.forwarded_blocks[edge.source.value]==raw.forwarded_blocks[edge.target.value]);
    ++pending[edge.source.value];++offset[edge.target.value+1];
  }
  for(std::size_t i=1;i<offset.size();++i){budget.Step();offset[i]+=offset[i-1];}
  auto cursor=std::pmr::vector<std::uint32_t>{offset,&memory};
  for(const auto& e:raw.edges){budget.Step();if(raw.forwarded_blocks[e.source.value])preds[cursor[e.target.value]++]=e.source;}
  for(std::uint32_t b=0;b<raw.blocks.size();++b)if(raw.forwarded_blocks[b]&&!pending[b]){budget.Step();Need(terminal[b]);good[b]=1;queue.push_back({b});}
  for(std::size_t i=0;i<queue.size();++i)for(auto p=offset[queue[i].value];p<offset[queue[i].value+1];++p){budget.Step();auto b=preds[p].value;Need(pending[b]);if(!--pending[b]){good[b]=1;queue.push_back({b});}}
  for(std::uint32_t b=0;b<raw.blocks.size();++b){budget.Step();if(raw.forwarded_blocks[b])Need(good[b]);}
  // A forwarded Catch has a typed path identity, never a fabricated physical
  // catch point. Every entry was included in the incoming-edge census above.
  for(std::uint32_t b=0;b<raw.blocks.size();++b) {
    budget.Step(); if(!raw.forwarded_blocks[b])continue;
    const auto& original=raw.enumerations[*raw.forwarded_blocks[b]];
    const auto& port=*flow.enumerations[original.statement.value];
    flow.original_phases[b]=FlowForwardedPhase{original.statement,WorkBlockId{b}};
    if(raw.phases[b].phase==ExceptionPhase::kCatch)flow.original_handlers[b]=FlowOriginalRouteCoverage::kComplete;
    const auto& block=raw.blocks[b];
    for(std::uint32_t j=0;j<block.op_count;++j) {
      budget.Step(); const auto op=block.first_op.value+j;
      if(raw.operations[op].forwarding_throw && exits[op]) {
        const auto& route=std::get<ForwardedExceptionRoute>(original.acquisition);
        flow.original_exceptions[*exits[op]]=FlowForwardedException{port.acquire_before,port.thrown,{*exits[op]},route.handler,route.payload};
      }
    }
  }
  for(const auto& input:raw.enumeration_inputs) {
    budget.Step(); Need(input.loop<raw.enumerations.size() && input.expression.value<flow.actions.size() &&
        input.use.value<raw.uses.size());
    const auto& native=raw.enumerations[input.loop];
    const auto& action=flow.actions[input.expression.value];
    Need(action && flow.events[action->before.value].kind==FlowEventKind::kRead &&
        flow.events[action->before.value].read==native.key);
  }
  // Reset-aware declaration availability, not a non-captured-key assumption.
  auto initialized=AnalyzeFlowInitializations(flow,writes,budget.Remaining());
  if(const auto* error=std::get_if<Error>(&initialized))throw *error;
  const auto& proof=std::get<FlowInitializations>(initialized);budget.Admit(proof.allocated_bytes,1);budget.Step(proof.work);
  for(const auto key:writes){budget.Step();Need(proof.Completed(key.binding,key.point));}
  // The raw next value denotes the current successful iteration only. A
  // recovery may not hoist that read before KeyCommit or onto exhaustion.
  for(const auto& input:raw.enumeration_inputs) {
    budget.Step();
    const auto& native=raw.enumerations[input.loop];
    const auto& ports=*flow.enumerations[native.statement.value];
    const auto before=flow.actions[input.expression.value]->before;
    if(proof.order.tree.nodes[before.value].begin)
      Need(proof.order.Dominates(ports.key_commit,before));
  }
}
static std::variant<TreeStorage,TreeStructureError> VerifyInitialProtocolRecovery(
    const FunctionTree& tree,const OriginalFlow& original,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);
    std::vector<std::optional<SymbolId>> aliases;
    std::vector<std::optional<PrimitiveId>> literals;
    std::vector<std::optional<ExprId>> replacements;
    std::vector<std::uint8_t> omitted,normal;
    std::vector<ExpressionEvaluation> effects;
    budget.Array(aliases,tree.symbols.size());budget.Array(literals,tree.symbols.size());
    budget.Array(replacements,tree.expressions.size());budget.Array(omitted,tree.statements.size());
    budget.Array(normal,tree.expressions.size());budget.Array(effects,tree.expressions.size());
    for(std::uint32_t i=0;i<aliases.size();++i){budget.Step();aliases[i]=SymbolId{i};}
    for(auto& effect:effects){budget.Step();effect=ExpressionEvaluation::kRequired;}
    LegacyFlowProjection projection{aliases,literals,replacements,omitted,effects,normal};projection.original=&original;
    auto result=BuildRecoveryFlow(tree,projection,budget.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&result))return *error;
    const auto& checked=std::get<RecoveryFlow>(result);
    budget.Admit(checked.allocated_bytes,1);budget.Step(checked.work);
    return TreeStorage{budget.Bytes(),budget.Work()};
  }catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
std::variant<TreeStorage,TreeStructureError> VerifyInitialForInRecovery(const FunctionTree& tree,const OriginalFlow& original,analysis::SemanticBudget budget){return VerifyInitialProtocolRecovery(tree,original,budget);}
std::variant<TreeStorage,TreeStructureError> VerifyInitialForOfRecovery(const FunctionTree& tree,const OriginalFlow& original,analysis::SemanticBudget budget){return VerifyInitialProtocolRecovery(tree,original,budget);}
} // namespace abcd::source
