#include "recovery_flow.h"
#include "expression_observation.h"
#include "binding_initializer.h"
#include "recovery_scratch.h"
#include "expression_children.h"
#include "loop_syntax.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace abcd::source {
namespace {
using Error=TreeStructureError;
using Point=FlowPoint;
struct HeaderProgram {
  StmtId loop;
  LoopCompletionTransfer transfer;
  std::optional<StmtId> retained;
  Point exit;
  FlowScopeContext scope;
};
struct LoopBodyProgram {std::uint32_t first;std::optional<StmtId> retained;};
struct Context {
  Point next,thrown;
  std::optional<Point> finalizer;
  std::optional<DiscardedId> discarded;
  FlowValueUse value_use=FlowValueUse::kObserved;
  std::optional<StmtId> owner;
  std::optional<SymbolId> transport;
  bool entry_evaluation=false;
  std::optional<HeaderProgram> header;
  FlowStatementRetention retention=FlowStatementRetention::kOptional;
  FlowLoopContext loop_scope=UnmodeledScope{};
  Context Operand(Point continuation,FlowValueUse use=FlowValueUse::kObserved) const {
    auto result=*this;result.next=continuation;result.value_use=use;return result;
  }
  Context Child(Point continuation,StmtId parent,FlowStatementRetention retained=FlowStatementRetention::kOptional) const {
    auto result=Operand(continuation);result.owner=parent;result.discarded.reset();result.entry_evaluation=false;result.retention=retained;return result;
  }
};
struct Statement {StmtId id;Context context;};
struct Expression {ExprId id;Context context;};
using Task=std::variant<Statement,Expression>;
enum class ReadState : std::uint8_t {kUnseen,kPending,kDone};
class FlowBuilder {
 public:
  FlowBuilder(const FunctionTree& tree,LegacyFlowProjection projection,analysis::SemanticBudget budget,RecoveryFlow storage)
      : tree_(tree), projection_(projection), budget_(budget), memory_(budget_),
        statements_(&memory_), expressions_(&memory_), exits_(&memory_), tasks_(&memory_), seen_(&memory_), reads_(&memory_), read_states_(&memory_), initializers_(&memory_), catch_parameters_(&memory_), scope_lifetimes_(&memory_), statement_scopes_(&memory_), loop_entries_(&memory_), native_bindings_(&memory_), loop_members_(&memory_), member_next_(&memory_), loop_bodies_(&memory_), repeats_(&memory_), result_(std::move(storage)) {
    events_=std::move(result_.events);events_.clear();edges_=std::move(result_.edges);edges_.clear();
    definitions_=std::move(result_.definitions);definitions_.clear();references_=std::move(result_.references);references_.clear();
    names_=std::move(result_.names);names_.clear();
    discarded_=std::move(result_.discarded);discarded_.clear();
    result_.properties.clear();
    result_.captured_inputs.clear();
  }
  RecoveryFlow Run() {
    result_.scopes.reset();
    result_.original=projection_.original;
    // Final truth facts belong to the old proposal. Only the common joint
    // solve may populate them; capacity reuse is not fact reuse.
    decltype(result_.normal_truths){}.swap(result_.normal_truths);
    std::optional<SourceCells> owned_cells;
    if(!projection_.cells){owned_cells=AnalyzeSourceCells(tree_,budget_);projection_.cells=&*owned_cells;}
    RequireTree(projection_.cells->owner==&tree_&&projection_.cells->access.size()==tree_.symbols.size(),Error::kOwnership);
    if(result_.original)CheckOriginalFlow(tree_,*result_.original);
    RequireTree(projection_.normal_kinds.empty()||projection_.normal_kinds.size()==tree_.expressions.size(),Error::kOwnership);
    Resize(result_.normal_kinds,projection_.normal_kinds.size());
    std::copy(projection_.normal_kinds.begin(),projection_.normal_kinds.end(),result_.normal_kinds.begin());
    Resize(result_.normal_exits,projection_.original?tree_.expressions.size():0);
    for(auto& exit:result_.normal_exits){budget_.Step();exit.reset();}
    // Native events/properties already own source-only completion semantics.
    // Retain the extra action index only while bridging raw phase identities;
    // an unconsumed copy across preparation revisions must not spend budget.
    const bool phased=projection_.original&&!projection_.original->phases.empty();
    if(!phased) {
      decltype(result_.actions){}.swap(result_.actions);
      decltype(result_.original_actions){}.swap(result_.original_actions);
      decltype(result_.original_phases){}.swap(result_.original_phases);
      decltype(result_.original_exceptions){}.swap(result_.original_exceptions);
      decltype(result_.original_handlers){}.swap(result_.original_handlers);
    }
    Resize(result_.actions,phased?tree_.expressions.size():0);
    Resize(result_.original_actions,phased?projection_.original->operations.size():0);
    for(auto& action:result_.original_actions){budget_.Step();action.reset();}
    for(auto& action:result_.actions){budget_.Step();action.reset();}
    if(phased)budget_.Array(abrupt_actions_,tree_.statements.size());
    RequireTree(projection_.statement_values.empty()||projection_.statement_values.size()==tree_.statements.size(),Error::kOwnership);
    RequireTree(projection_.forwarded_values.empty()||projection_.forwarded_values.size()==tree_.statements.size(),Error::kOwnership);
    RequireTree(projection_.loop_declarations.empty()||projection_.loop_declarations.size()==tree_.statements.size(),Error::kOwnership);
    RequireTree(projection_.stable_entries.empty()||projection_.stable_entries.size()==tree_.symbols.size(),Error::kOwnership);
    RequireTree(projection_.read_bindings.empty()||projection_.read_bindings.size()==tree_.symbols.size(),Error::kOwnership);
    RequireTree(projection_.read_sources.empty()||projection_.read_sources.size()==tree_.expressions.size(),Error::kOwnership);
    RequireTree(projection_.normal_bindings.empty()||projection_.normal_bindings.size()==tree_.expressions.size(),Error::kOwnership);
    RequireTree(projection_.source_scopes.loop_owners.empty()||projection_.source_scopes.loop_owners.size()==tree_.statements.size(),Error::kOwnership);
    // Current production flow consumes shared normal-result facts. Unproved
    // trial/legacy projections retain only the direct-read fallback.
    if(projection_.normal_bindings.empty())ProjectedReads();
    initializers_.resize(tree_.expressions.size());
    catch_parameters_.resize(tree_.statements.size());
    scope_lifetimes_.resize(tree_.scopes.size());
    statement_scopes_.resize(tree_.statements.size(),UnmodeledScope{});
    for (std::size_t i=0;i<projection_.initializers.size();++i) {
      budget_.Step();
      if (const auto& initializer=projection_.initializers[i])
        initializers_[initializer->assignment.value]=StmtId{static_cast<std::uint32_t>(i)};
    }
    Resize(result_.statement_semantics,tree_.statements.size());
    for(auto& statement:result_.statement_semantics){budget_.Step();statement.reset();}
    result_.statement_order.clear();
    Resize(result_.loops,projection_.loops?tree_.statements.size():0);
    for(auto& loop:result_.loops){budget_.Step();loop.reset();}
    if(projection_.loops) {
      RequireTree(projection_.loops->conditions.size()==tree_.statements.size() &&
          projection_.loops->transitions.size()==tree_.statements.size() &&
          projection_.loops->tail_starts.size()==tree_.statements.size(),Error::kOwnership);
    }
    budget_.Step(tree_.statements.size());loop_bodies_.resize(tree_.statements.size());
    budget_.Step(tree_.statements.size());repeats_.resize(tree_.statements.size());
    // Grammar owns the retained entry of each native environment. Index once;
    // no optimizer infers first allocation from AST/CFG adjacency or ScopeId.
    loop_entries_.resize(tree_.statements.size());
    native_bindings_.resize(tree_.symbols.size());
    budget_.Step(tree_.statements.size());loop_members_.resize(tree_.statements.size());
    budget_.Step(tree_.symbols.size());member_next_.resize(tree_.symbols.size());
    RequireTree(projection_.source_scopes.loop_allocations.empty() || projection_.source_scopes.loop_allocations.size()==tree_.symbols.size(),Error::kOwnership);
    for(std::size_t i=0;i<projection_.source_scopes.loop_allocations.size();++i) {
      budget_.Step();if(const auto& native=projection_.source_scopes.loop_allocations[i]) {
        RequireTree(native->entry.value<loop_entries_.size() && native->loop.value<loop_entries_.size(),Error::kOwnership);
        RequireTree(!projection_.omitted[native->entry.value] && std::holds_alternative<LoopStmt>(tree_.statements[native->loop.value].statement),Error::kOwnership);
        auto& owner=loop_entries_[native->entry.value];RequireTree(!owner||owner==native->loop,Error::kOwnership);owner=native->loop;
        const auto binding=Resolve(SymbolId{static_cast<std::uint32_t>(i)});
        RequireTree(binding.value<native_bindings_.size(),Error::kOwnership);
        auto& member=native_bindings_[binding.value];
        RequireTree(!member||(member->loop==native->loop&&member->entry==native->entry),Error::kOwnership);
        if(!member){member_next_[binding.value]=loop_members_[native->loop.value];loop_members_[native->loop.value]=binding;}
        member=*native;
      }
    }
    native_scopes_=projection_.loops&&std::any_of(native_bindings_.begin(),native_bindings_.end(),[](const auto& member){return member.has_value();});
    if(native_scopes_){budget_.Step(tree_.statements.size());Resize(loop_scopes_,tree_.statements.size());}
    terminal_=New(); dispatch_=New();
    if(native_scopes_)loop_scope_events_[terminal_.value]={IncomingScope{},IncomingScope{}};
    current_loop_=IncomingScope{};
    // Conservative cleanup can lose nested-unit facts, never the invocation's
    // body activation. Completion uncertainty does not allocate a new body cell.
    scope_events_[dispatch_.value]={EnclosingScope{tree_.body_scope},EnclosingScope{tree_.body_scope}};
    if(native_scopes_)loop_scope_events_[dispatch_.value]={UnmodeledScope{},UnmodeledScope{}};
    statements_.resize(tree_.statements.size()); exits_.resize(tree_.statements.size());
    expressions_.resize(tree_.expressions.size()); seen_.resize(tree_.expressions.size());
    Resize(result_.value_uses,tree_.expressions.size());
    Resize(result_.expression_owners,tree_.expressions.size());
    for(auto& owner:result_.expression_owners){budget_.Step();owner.reset();}
    Resize(result_.expression_transports,projection_.transported_reads.empty()?0:tree_.expressions.size());
    for(auto& root:result_.expression_transports){budget_.Step();root.reset();}
    for(auto& use:result_.value_uses){budget_.Step();use=FlowValueUse::kUnmodeled;}
    for(std::size_t i=0;i<statements_.size();++i){budget_.Step();statements_[i]=New();repeats_[i]=statements_[i];}
    for(auto& point:expressions_){budget_.Step();point=New();}
    Link(dispatch_,terminal_,FlowCompletion::kConservativeCleanup);
    entry_=New();auto previous=entry_;
    for(std::size_t i=0;i<tree_.symbols.size();++i) {
      budget_.Step();if(tree_.symbols[i].kind!=BindingKind::kFunctionSelf)continue;
      const auto initialized=Define(SymbolId{static_cast<std::uint32_t>(i)},FlowDefinitionKind::kFunctionSelf);
      Link(previous,initialized);previous=initialized;
    }
    for(const auto& parameter:tree_.parameters) {
      budget_.Step();const auto initialized=Define(parameter.symbol,FlowDefinitionKind::kParameter);
      Link(previous,initialized);previous=initialized;
    }
    Link(previous,statements_[tree_.body.value]);
    Context body{terminal_,terminal_,{}};body.retention=FlowStatementRetention::kRequired;
    tasks_.push_back(Statement{tree_.body,body});
    while(!tasks_.empty()) {budget_.Step();const auto task=tasks_.back();tasks_.pop_back();std::visit([&](auto at){Build(at);},task);}
    auto& result=result_;
    result.identity=tree_.identity;result.value_revision=tree_.value_revision;result.cfg_revision=tree_.cfg_revision;
    result.entry=entry_;result.terminal=terminal_;
    auto flow_scopes=MakeFlowScopes(tree_,budget_);flow_scopes->events=std::move(scope_events_);
    if(native_scopes_){IndexFlowLoopScopes(*flow_scopes,loop_scopes_,budget_);flow_scopes->loop_events=std::move(loop_scope_events_);}
    result.scopes=std::move(flow_scopes);
    const auto copy=[&](auto& destination,const auto& source){Resize(destination,source.size());std::copy(source.begin(),source.end(),destination.begin());};
    result.events=std::move(events_);result.edges=std::move(edges_);
    result.definitions=std::move(definitions_);result.references=std::move(references_);result.names=std::move(names_);
    result.discarded=std::move(discarded_);
    if(result.original)for(auto& reference:result.references) {
      budget_.Step();if(const auto* expression=std::get_if<ExprId>(&reference.site)) {
        const auto range=result.original->input_ranges[expression->value];
        if(range.count)reference.original=range;
      }
    }
    copy(result.statements,statements_);copy(result.expressions,expressions_);
    bool controls=false;
    if(result.original)for(const auto& operation:result.original->operations) {
      budget_.Step();if(operation.life==analysis::ValueLife::kLive&&operation.control){controls=true;break;}
    }
    if(!controls)decltype(result.original_controls){}.swap(result.original_controls);
    Resize(result.original_controls,controls?result.original->operations.size():0);
    for(std::size_t i=0;i<result.original_controls.size();++i) {
      budget_.Step();auto& occurrence=result.original_controls[i];occurrence.reset();
      const auto& operation=result.original->operations[i];
      if(operation.life!=analysis::ValueLife::kLive||!operation.control)continue;
      const auto& input=*operation.control;
      RequireTree(input.expression.value<result.normal_exits.size(),Error::kOwnership);
      if(const auto normal=result.normal_exits[input.expression.value])
        occurrence=FlowOriginalControl{input.expression,expressions_[input.expression.value],*normal,input.kind,input.test};
    }
    // Publish each native action once. Phase and exception consumers use this
    // same index; they must not independently guess an action from an anchor.
    for(std::size_t i=0;i<result.original_actions.size();++i) {
      budget_.Step();const auto& operation=result.original->operations[i];
      if(operation.life!=analysis::ValueLife::kLive)continue;
      auto& current=result.original_actions[i];
      if(operation.abrupt&&operation.statement) {
        RequireTree(operation.statement->value<abrupt_actions_.size(),Error::kOwnership);
        if(const auto& action=abrupt_actions_[operation.statement->value];action&&
            action->completion==(*operation.abrupt==OriginalAbruptKind::kThrow?FlowCompletion::kThrow:FlowCompletion::kReturn))
          {RequireTree(!current,Error::kOwnership);current=FlowOriginalAction{action->point,
              action->completion==FlowCompletion::kReturn&&!action->exceptions.finalizer?
                  std::optional{action->destination}:std::nullopt,action->exceptions};}
      } else if(operation.intrinsic_expression) {
        RequireTree(operation.intrinsic_expression->value<result.actions.size(),Error::kOwnership);
        if(const auto& action=result.actions[operation.intrinsic_expression->value]) {
          RequireTree(!current,Error::kOwnership);
          auto normal=std::optional{action->normal};
          // The action consumes operands; a separately verified result may
          // complete its owning projection elsewhere in a composite expression.
          // Raw Commit follows that result, never an enclosing coverage anchor.
          if(const auto value=FindOriginalResult(*result.original,analysis::OpId{static_cast<unsigned>(i)})) {
            RequireTree(value->expression.value<result.normal_exits.size(),Error::kOwnership);
            normal=result.normal_exits[value->expression.value];
          }
          current=FlowOriginalAction{action->before,normal,action->exceptions};
        }
      }
    }
    Resize(result.original_phases,result.original?result.original->phases.size():0);
    for(std::size_t i=0;i<result.original_phases.size();++i) {
      budget_.Step();auto& point=result.original_phases[i];point.reset();
      const auto& phase=result.original->phases[i];
      if(!phase.operation)continue;
      RequireTree(phase.operation->value<result.original->operations.size(),Error::kOwnership);
      const auto& operation=result.original->operations[phase.operation->value];
      if(operation.life!=analysis::ValueLife::kLive)continue;
      const auto& action=result.original_actions[phase.operation->value];
      switch(phase.phase) {
        case analysis::ExceptionPhase::kBefore:
          if(action)point=action->before;
          break;
        case analysis::ExceptionPhase::kCommit:
          if(action)point=action->normal;
          else if(operation.result_expression) {
            RequireTree(operation.result_expression->value<result.normal_exits.size(),Error::kOwnership);
            point=result.normal_exits[operation.result_expression->value];
          }
          break;
        case analysis::ExceptionPhase::kCatch:break;
      }
    }
    for(const auto& definition:result.definitions) {
      budget_.Step();if(!definition.original_catch||!definition.catch_entry)continue;
      const auto handler=definition.original_catch->handler;
      if(handler.value<result.original_phases.size()&&result.original->phases[handler.value].phase==analysis::ExceptionPhase::kCatch)
        result.original_phases[handler.value]=definition.catch_entry;
    }
    Resize(result.original_handlers,result.original_phases.size());
    for(std::size_t i=0;i<result.original_handlers.size();++i) {
      budget_.Step();result.original_handlers[i]=
          FindOriginalPhase(result,analysis::WorkBlockId{static_cast<std::uint32_t>(i)},analysis::ExceptionPhase::kCatch)?
          FlowOriginalRouteCoverage::kComplete:FlowOriginalRouteCoverage::kUnmodeled;
    }
    Resize(result.original_exceptions,result.original_phases.empty()?0:result.original->exceptional_exits.size());
    for(std::size_t i=0;i<result.original_exceptions.size();++i) {
      budget_.Step();auto& route=result.original_exceptions[i];route.reset();
      const auto& exit=result.original->exceptional_exits[i];
      RequireTree(exit.op.value<result.original->operations.size(),Error::kOwnership);
      const auto& operation=result.original->operations[exit.op.value];
      if(operation.life!=analysis::ValueLife::kLive)continue;
      const auto& action=result.original_actions[exit.op.value];
      const auto before=FindOriginalPhase(result,operation.block,analysis::ExceptionPhase::kBefore,exit.op);
      const auto target=exit.handler?FindOriginalPhase(result,exit.handler->entry,analysis::ExceptionPhase::kCatch):std::optional{result.terminal};
      // Same selected handler and completion context are semantic obligations.
      // An imported region has no finally: a newly projected cleanup cannot
      // borrow its pending-exception proof. Missing correspondence is a gap.
      if(before&&target&&action&&action->exceptions.thrown==*target&&!action->exceptions.finalizer)
        route=FlowOriginalException{*before,*target};
      if(exit.handler&&!route) {
        RequireTree(exit.handler->entry.value<result.original_handlers.size(),Error::kOwnership);
        result.original_handlers[exit.handler->entry.value]=FlowOriginalRouteCoverage::kUnmodeled;
      }
    }
    Resize(result.original_results,result.original?result.original->operations.size():0);
    for(std::size_t i=0;i<result.original_results.size();++i) {
      budget_.Step();auto& occurrence=result.original_results[i];occurrence.reset();
      const auto original=FindOriginalResult(*result.original,analysis::OpId{static_cast<std::uint32_t>(i)});
      if(!original)continue;
      const auto expression=original->expression;
      RequireTree(expression.value<result.normal_exits.size(),Error::kOwnership);
      const auto normal=original->commit?FindOriginalPhase(result,*original->commit,analysis::ExceptionPhase::kCommit,
          analysis::OpId{static_cast<std::uint32_t>(i)}):result.normal_exits[expression.value];
      if(normal)occurrence=FlowOriginalResult{expression,expressions_[expression.value],*normal};
    }
    Resize(result.original_effects,result.original?tree_.expressions.size():0);
    for(auto& effects:result.original_effects){budget_.Step();effects.reset();}
    if(result.original)for(std::size_t i=0;i<result.original->operations.size();++i) {
      const auto& operation=result.original->operations[i];
      budget_.Step();if(operation.life!=analysis::ValueLife::kLive||!operation.intrinsic_expression)continue;
      // An imported phased action must have its own Before correspondence.
      // Ordinary SSA has no phase table. This obligation excludes borrowing
      // a result/control anchor to suppress a native intrinsic's effects.
      if(!result.original->phases.empty()&&!FindOriginalPhase(result,operation.block,analysis::ExceptionPhase::kBefore,
          analysis::OpId{static_cast<std::uint32_t>(i)}))continue;
      RequireTree(operation.intrinsic_expression->value<result.original_effects.size(),Error::kOwnership);
      auto& effects=result.original_effects[operation.intrinsic_expression->value];
      const SourceEffects proof{operation.normal,operation.before_throw,operation.exceptional};
      effects=effects?IntersectSourceEffects(*effects,proof):proof;
    }
    const auto& scopes=projection_.source_scopes;
    RequireTree(scopes.occurrences.empty() || scopes.occurrences.size()==tree_.statements.size(),Error::kOwnership);
    RequireTree(scopes.bindings.empty() || scopes.bindings.size()==tree_.symbols.size(),Error::kOwnership);
    RequireTree(scopes.allocations.empty() || scopes.allocations.size()==scopes.regions.size(),Error::kOwnership);
    RequireTree(scopes.unique_spellings.empty() || scopes.unique_spellings.size()==tree_.symbols.size(),Error::kOwnership);
    RequireTree(scopes.spellings.empty()||scopes.spellings.size()==tree_.symbols.size(),Error::kOwnership);
    RequireTree(scopes.names.empty()||scopes.names.size()==tree_.names.size(),Error::kOwnership);
    Resize(result.source_regions,scopes.regions.size());std::copy(scopes.regions.begin(),scopes.regions.end(),result.source_regions.begin());
    Resize(result.source_occurrences,scopes.occurrences.size());std::copy(scopes.occurrences.begin(),scopes.occurrences.end(),result.source_occurrences.begin());
    copy(result.source_spellings,scopes.spellings);copy(result.source_name_spellings,scopes.names);
    Resize(result.bindings,tree_.symbols.size());
    for(std::size_t i=0;i<tree_.symbols.size();++i) {
      budget_.Step();result.bindings[i]={tree_.symbols[i].kind,
          !projection_.private_bindings.empty() && projection_.private_bindings[i]
              ? FlowBindingExposure::kPrivate : FlowBindingExposure::kObservable};
      const auto scope=tree_.symbols[i].scope;
      if(scope_lifetimes_[scope.value]) {
        result.bindings[i].lifetime=*scope_lifetimes_[scope.value];
        result.bindings[i].allocation=scope_lifetimes_[scope.value]->enter;
      }
      else switch(tree_.symbols[i].kind) {
        case BindingKind::kParameter:case BindingKind::kFunctionSelf:
        case BindingKind::kCapture:case BindingKind::kModule:
          result.bindings[i].lifetime=EnclosingBindingLifetime{};break;
        case BindingKind::kLocal:case BindingKind::kCompilerTemporary:
        case BindingKind::kCatchParameter:
          result.bindings[i].lifetime=UnmodeledBindingLifetime{};break;
      }
      if(!projection_.stable_entries.empty())result.bindings[i].stable_entry=projection_.stable_entries[i]!=0;
      if(!scopes.unique_spellings.empty())result.bindings[i].unique_spelling=scopes.unique_spellings[i]!=0;
      if(!scopes.bindings.empty()) {
        result.bindings[i].scope=scopes.bindings[i];
        if(const auto region_id=result.BindingRegion(SymbolId{static_cast<std::uint32_t>(i)})) {
          RequireTree(region_id->value<result.source_regions.size(),Error::kOwnership);
          const auto& region=result.source_regions[region_id->value];
          RequireTree(!scopes.occurrences.empty()&&region.first<region.last,Error::kOwnership);
          if(std::holds_alternative<ProjectedBindingScope>(result.bindings[i].scope)) {
            // Current source ownership replaces the original allocation site.
            // Lexical containment alone cannot certify a projected unit.
            result.bindings[i].lifetime=UnmodeledBindingLifetime{};result.bindings[i].allocation.reset();
            if(!scopes.allocations.empty())std::visit([&](const auto& allocation) {
              using T=std::decay_t<decltype(allocation)>;
              if constexpr(std::is_same_v<T,ScopeId>) {
                RequireTree(allocation.value<scope_lifetimes_.size(),Error::kOwnership);
                if(scope_lifetimes_[allocation.value]) {
                  result.bindings[i].lifetime=*scope_lifetimes_[allocation.value];
                  result.bindings[i].allocation=scope_lifetimes_[allocation.value]->enter;
                }
              } else if constexpr(std::is_same_v<T,LegacyLoopAllocation>)
                result.bindings[i].lifetime=CompletionOnlyLifetime{};
              else {static_assert(std::is_same_v<T,UnmodeledSourceAllocation>);}
            },scopes.allocations[region_id->value]);
          }
        }
      }
      if(const auto& native=native_bindings_[i]) {
        const auto point=statements_[native->entry.value];
        RequireTree(result.events[point.value].kind==FlowEventKind::kEnterLoopEnvironment && result.events[point.value].loop_environment==native->loop,Error::kOwnership);
        result.bindings[i].allocation=point;
        if(native_scopes_&&result.loops[native->loop.value]&&result.loops[native->loop.value]->entry_allocation)
          result.bindings[i].lifetime=LoopBindingLifetime{native->loop};
        else result.bindings[i].lifetime=CompletionOnlyLifetime{};
      }
    }
    // Storage composes identities; immutable read aliases do not manufacture
    // closed-cell proofs. Any externally reachable canonical member taints its
    // resolved slot. The canonical producer is shared by current/trial owners.
    for(std::size_t i=0;i<result.bindings.size();++i){budget_.Step();result.bindings[i].cell_access=projection_.cells->access[i];}
    for(std::size_t i=0;i<result.bindings.size();++i){
      budget_.Step();const auto root=Resolve(SymbolId{static_cast<std::uint32_t>(i)});
      RequireTree(root.value<result.bindings.size(),Error::kBinding);
      auto& access=result.bindings[root.value].cell_access;
      access=JoinCellVisibility(*access,projection_.cells->access[i]);
    }
    for(auto& binding:result.bindings){budget_.Step();if(binding.MayBeChangedByUserCode())binding.stable_entry=false;}
    for(const auto& definition:result.definitions) {
      budget_.Step();auto& binding=result.bindings[definition.binding.value];
      if(definition.kind==FlowDefinitionKind::kInitializer ||
          (definition.kind==FlowDefinitionKind::kUndefined && !binding.declaration))
        {binding.declaration=definition.declaration;binding.initialization=definition.kind;}
    }
    // Supplied native declarations supersede original empty placeholders even
    // after dead-value removal changes Initialize(value) to Initialize(undefined).
    // Declaration ownership comes from the plan, never definition arena order.
    for(const auto& definition:result.definitions) {
      budget_.Step();if(!definition.declaration)continue;
      const auto statement=definition.declaration->value;
      const bool native=!projection_.initializers.empty() && projection_.initializers[statement].has_value();
      const bool outlined=!projection_.declaration_completions.empty() &&
          projection_.declaration_completions[statement] &&
          projection_.declaration_completions[statement]->kind==DeclarationCompletionKind::kInitialize;
      if((native||outlined) && (definition.kind==FlowDefinitionKind::kInitializer || definition.kind==FlowDefinitionKind::kUndefined)) {
        auto& binding=result.bindings[definition.binding.value];
        binding.declaration=definition.declaration;binding.initialization=definition.kind;
        if(native&&scopes.bindings.empty())binding.scope=UnmodeledBindingScope{};
      }
    }
    auto analyzed_names=AnalyzeFlowBindingNames(result,scopes.shadows,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&analyzed_names))throw *error;
    auto names=std::get<FlowBindingNames>(std::move(analyzed_names));
    budget_.Admit(names.allocated_bytes,1);budget_.Step(names.work);
    result.source_shadows=std::move(names.shadows);result.binding_presence=std::move(names.presence);
    result.source_names=std::move(names.index);
    result.allocated_bytes=budget_.Bytes();result.work=budget_.Work();return std::move(result);
  }
 private:
  template<class T> void Resize(std::vector<T>& values,std::size_t count) {
    if(count>values.capacity()) {
      budget_.Admit(count,sizeof(T));values.reserve(count);
      budget_.Admit(values.capacity()-count,sizeof(T));
    }
    budget_.Step(count);values.resize(count);
  }
  void WriteAction(const FlowDefinition& definition,FlowAction native) {
    // Action correspondence belongs only to checked phased writes. Do not
    // enlarge every scalar definition or retain an unconsumed native copy.
    if(result_.original_actions.empty()||!definition.original)return;
    budget_.Step();const auto* saved=std::get_if<OriginalSavedDefinition>(&definition.original->origin);
    const auto* operation=saved?std::get_if<analysis::OpId>(&saved->placement):nullptr;
    if(!operation||operation->value>=result_.original_actions.size())return;
    const auto& original=result_.original->operations[operation->value];
    // A staged value does not own a physical write. Match the independently
    // checked commit identity, preserving assignment-backed declarations.
    if(original.life!=analysis::ValueLife::kLive||!original.write||definition.WriteSite()!=original.write)return;
    auto& action=result_.original_actions[operation->value];RequireTree(!action,Error::kOwnership);
    action=FlowOriginalAction{native.before,native.normal,native.exceptions};
  }
  template<class T> void Append(std::vector<T>& values,T value) {
    // The published graph owns these buffers directly. Charge geometric growth
    // including old/new overlap, then move ownership instead of allocating a
    // second copy at publication. Scratch still uses the scoped PMR resource.
    if(values.size()==values.capacity()) {
      constexpr std::size_t growth=2;
      RequireTree(values.size()<values.max_size(),Error::kResourceLimit);
      const auto capacity=values.capacity()?values.capacity()>values.max_size()/growth?
          values.max_size():values.capacity()*growth:1;
      budget_.Admit(capacity,sizeof(T));values.reserve(capacity);
      budget_.Admit(values.capacity()-capacity,sizeof(T));
    }
    budget_.Step();values.push_back(std::move(value));
  }
  Point New(FlowEvent event={}) {
    RequireTree(events_.size()<std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
    event.provenance=FlowProvenance::kPreserved;
    budget_.Step();event.owner=current_owner_;event.entry_evaluation=current_entry_evaluation_;Point result{static_cast<std::uint32_t>(events_.size())};Append(events_,event);Append(scope_events_,{current_scope_,current_scope_});
    if(native_scopes_)Append(loop_scope_events_,{current_loop_,current_loop_});return result;
  }
  Point Define(SymbolId binding,FlowDefinitionKind kind,std::optional<SymbolId> copy={},
      std::optional<ExprId> value={},std::optional<StmtId> declaration={},
      std::optional<ExprId> write={},FlowWriteEffect effect=FlowWriteEffect::kObservable,
      std::optional<DiscardedId> discarded={},std::optional<FlowExceptionContext> exceptions={},
      std::optional<StmtId> owner={}) {
    RequireTree(definitions_.size()<std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
    const auto point=New({FlowEventKind::kDefinition,{},FlowDefinitionId{static_cast<std::uint32_t>(definitions_.size())}});
    events_[point.value].effects=SourceEffects{analysis::LanguageEffect::kLexicalWrite};
    events_[point.value].discarded=discarded;
    Append(definitions_,{point,Resolve(binding),kind,copy,value,declaration,write,effect});
    if(result_.original) {
      const OriginalSourceDefinition* original=nullptr;
      // Assignment-backed declaration projections retain their original
      // write occurrence. Native declarations use their own checked commit,
      // never the initializer's expression identity as a surrogate write.
      if(write&&!result_.original->definitions.empty()) {
        const auto& fact=result_.original->definitions[write->value];
        if(fact)original=&*fact;
      } else if(declaration&&!result_.original->declarations.empty()) {
        const auto& fact=result_.original->declarations[declaration->value];
        if(fact)original=&*fact;
      }
      if(original&&original->binding==binding&&value==original->input)
        definitions_.back().original=original;
    }
    if(write || kind==FlowDefinitionKind::kUndefinedStore ||
        kind==FlowDefinitionKind::kInitializer || kind==FlowDefinitionKind::kAssignment)
      if(const auto site=definitions_.back().WriteSite())
        Reference(point,Resolve(binding),FlowReferenceKind::kWrite,*site,exceptions,owner);
    return point;
  }
  struct IterationProgram {Point start,allocation;};
  std::optional<IterationProgram> IterationCopies(StmtId loop,Point next) {
    if(!loop_members_[loop.value])return {};
    // ECMA-262 2025 14.7.4.3/.4: first and normal/continue unit replacement.
    // Capture old scalar values before changing CURRENT units. The spec builds
    // an unexposed environment earlier; these plain lexical reads/initializes
    // run no user code before publication. This models scalar epochs, not heap
    // allocation order or captured-cell equality. Never read from the new unit.
    std::pmr::vector<FlowCapturedValue> inputs(&memory_);
    const auto start=New();auto previous=start;
    for(auto binding=loop_members_[loop.value];binding;binding=member_next_[binding->value]) {
      budget_.Step();const auto read=New({FlowEventKind::kCopyInput,*binding});
      Reference(read,*binding,FlowReferenceKind::kRead,loop,{},loop);
      Link(previous,read);previous=read;inputs.push_back({read,*binding});
    }
    FlowEvent event{FlowEventKind::kEnterIterationEnvironment};event.loop_environment=loop;
    const auto allocation=New(event);Link(previous,allocation);previous=allocation;
    for(const auto input:inputs) {
      budget_.Step();const auto commit=Define(input.binding,FlowDefinitionKind::kIterationCopy,{},{},{},{},FlowWriteEffect::kInert);
      definitions_.back().input=input;Link(previous,commit);previous=commit;
    }
    Link(previous,next);return IterationProgram{start,allocation};
  }
  void Reference(FlowPoint point,SymbolId binding,FlowReferenceKind kind,FlowReferenceSite site,std::optional<FlowExceptionContext> exceptions={},std::optional<StmtId> owner={}) {
    budget_.Step();Append(references_,{point,binding,kind,site,exceptions,owner});
  }
  void Link(Point from,Point to,FlowCompletion completion=FlowCompletion::kNormal) {
    budget_.Step();Append(edges_,{from,to,completion,events_[to.value].incoming,events_[from.value].outgoing});
    events_[to.value].incoming=events_[from.value].outgoing=edges_.size()-1;
  }
  SymbolId Resolve(SymbolId symbol) const {
    return BindingProjection{projection_.aliases,projection_.storage,projection_.read_bindings}.Storage(symbol);
  }
  void ProjectedReads() {
    reads_.resize(tree_.expressions.size());read_states_.resize(tree_.expressions.size());
    std::pmr::vector<ExprId> path(&memory_);
    // Resolve replacement chains once, including forwarded copy RHSs. Arena
    // order is immaterial and each edge is visited once, not per affinity.
    for (std::size_t i=0;i<tree_.expressions.size();++i) {
      budget_.Step();ExprId id{static_cast<std::uint32_t>(i)};path.clear();
      while (read_states_[id.value]==ReadState::kUnseen) {
        budget_.Step();read_states_[id.value]=ReadState::kPending;path.push_back(id);
        if(!projection_.transports.empty()&&projection_.transports[id.value]) {
          read_states_[id.value]=ReadState::kDone;break;
        }
        if (const auto replacement=projection_.replacements[id.value]) {id=*replacement;continue;}
        if (const auto* read=std::get_if<SymbolExpr>(&tree_.expressions[id.value].expression);
            read&&!projection_.ReadLiteral(id,read->symbol))reads_[id.value]=ResolveRead(id,read->symbol);
        read_states_[id.value]=ReadState::kDone;break;
      }
      RequireTree(read_states_[id.value]==ReadState::kDone,Error::kOwnership);
      for (const auto at:path) {budget_.Step();reads_[at.value]=reads_[id.value];read_states_[at.value]=ReadState::kDone;}
    }
  }
  SymbolId ResolveRead(ExprId expression,SymbolId symbol) const {
    const auto target=BindingProjection{projection_.aliases,projection_.storage,projection_.read_bindings,projection_.read_sources}.Read(expression,symbol);
    RequireTree(target.value<tree_.symbols.size(),Error::kBinding);return target;
  }
  std::optional<SymbolId> CopyValue(ExprId id) const {
    if(projection_.normal_bindings.empty())return reads_[id.value];
    const auto result=projection_.normal_bindings[id.value];
    if(result)RequireTree(result->value<tree_.symbols.size(),Error::kBinding);
    return result;
  }
  Point Expr(ExprId id,Context context){tasks_.push_back(Expression{id,context});return expressions_[id.value];}
  Point Property(ExprId site,ExprId base,MemberKey key,FlowReferenceKind kind,Point access,const Context& context) {
    RequireTree(result_.properties.size()<std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
    const FlowPropertyId id{static_cast<std::uint32_t>(result_.properties.size())};
    const auto acquisition=New({FlowEventKind::kPropertyReference,{},{},site});
    events_[acquisition.value].property=FlowPropertySite{id,FlowPropertyPhase::kAcquisition};
    events_[acquisition.value].effects=SourceEffects{};
    events_[access.value].property=FlowPropertySite{id,FlowPropertyPhase::kAccess};
    const auto phase=[&](FlowPropertyPhase phase,SourceEffects effects) {
      const auto evaluation=effects.Evaluation();
      const auto point=New({FlowEventKind::kPropertyReference,{},{},site});
      events_[point.value].property=FlowPropertySite{id,phase};events_[point.value].discarded=context.discarded;
      events_[point.value].effect=evaluation;
      events_[point.value].effects=effects;
      if(evaluation==ExpressionEvaluation::kRequired) {
        Link(point,context.thrown,FlowCompletion::kThrow);
        if(context.discarded)discarded_[context.discarded->value].effect=evaluation;
      }
      return point;
    };
    const auto resources=ResourceFailures(tree_.identity.contract);
    const auto receiver=phase(FlowPropertyPhase::kReceiver,NormalPropertyReceiverEffects(base,result_.normal_kinds,resources));
    const auto conversion=phase(FlowPropertyPhase::kKey,NormalPropertyKeyEffects(key,result_.normal_kinds,resources));
    if(!result_.actions.empty())result_.actions[site.value]->before=receiver;
    Link(receiver,conversion);Link(conversion,access);
    FlowPropertyReference reference{acquisition,receiver,conversion,access,context.next,kind,site,base,key,{context.thrown,context.finalizer},context.owner};
    if(result_.original) {
      const auto range=result_.original->input_ranges[base.value];if(range.count)reference.original_base=range;
      if(const auto* computed=std::get_if<ComputedKey>(&key)) {
        const auto range=result_.original->input_ranges[computed->expression.value];if(range.count)reference.original_key=range;
      }
    }
    Append(result_.properties,std::move(reference));return acquisition;
  }
  Point Stmt(StmtId id,Context context){tasks_.push_back(Statement{id,context});return statements_[id.value];}
  Point Sequence(StmtId block_id,std::uint32_t first,std::uint32_t end,const Context& context,StmtId parent) {
    const auto& block=std::get<BlockStmt>(tree_.statements[block_id.value].statement);
    RequireTree(first<=end && end<=block.child_count,Error::kOwnership);
    auto next=context.next;
    for(auto i=end;i>first;--i){budget_.Step();next=Stmt(tree_.children[block.first_child+i-1],context.Child(next,parent));}
    return next;
  }
  void Abrupt(Point point,Point target,const Context& context,FlowCompletion completion) {
    Link(point,target,completion); // Bypass is a may-path, never evidence that cleanup is absent.
    if(context.finalizer){Link(point,*context.finalizer,FlowCompletion::kConservativeCleanup);Link(dispatch_,target,FlowCompletion::kConservativeCleanup);}
  }
  void Build(Expression at) {
    RequireTree(!seen_[at.id.value],Error::kOwnership);seen_[at.id.value]=1;
    const auto start=expressions_[at.id.value];const auto& context=at.context;
    current_owner_=context.owner;current_entry_evaluation_=context.entry_evaluation;
    current_scope_=context.owner?statement_scopes_[context.owner->value]:FlowScopeContext{UnmodeledScope{}};
    current_loop_=context.loop_scope;
    scope_events_[start.value]={current_scope_,current_scope_};
    if(native_scopes_)loop_scope_events_[start.value]={current_loop_,current_loop_};
    events_[start.value].owner=context.owner;events_[start.value].entry_evaluation=context.entry_evaluation;
    // Publish edits where their current execution boundary is built. Value
    // solvers must not rediscover projection facets from a mutable plan.
    const auto& source=tree_.expressions[at.id.value].expression;
    const auto present=[&](const auto& values){return !values.empty()&&bool(values[at.id.value]);};
    if(present(projection_.replacements)||present(projection_.transports)||present(projection_.read_sources)||
        present(projection_.read_literals)||IntrinsicLexicalDependency(source)!=LexicalDependency::kReferences||
        std::holds_alternative<GlobalNameExpr>(source))events_[start.value].provenance=FlowProvenance::kChanged;
    if(const auto* read=std::get_if<SymbolExpr>(&source)) {
      const auto edited=[&](const auto& values){return !values.empty()&&bool(values[read->symbol.value]);};
      if(edited(projection_.aliases)||edited(projection_.storage)||edited(projection_.read_bindings)||edited(projection_.literals))
        events_[start.value].provenance=FlowProvenance::kChanged;
    }
    if(!result_.normal_exits.empty())result_.normal_exits[at.id.value]=context.next;
    result_.value_uses[at.id.value]=context.value_use;
    result_.expression_owners[at.id.value]=context.owner;
    if(!result_.expression_transports.empty())result_.expression_transports[at.id.value]=context.transport;
    if(!projection_.transports.empty()&&projection_.transports[at.id.value]) {
      Link(start,context.next);return;
    }
    if(auto replacement=projection_.replacements[at.id.value]) {
      events_[expressions_[replacement->value].value].provenance=FlowProvenance::kChanged;
      auto destination=context;
      if(!destination.transport&&!projection_.transported_reads.empty())
        destination.transport=projection_.transported_reads[at.id.value];
      Link(start,Expr(*replacement,destination));return;
    }
    const auto& node=tree_.expressions[at.id.value].expression;
    if(const auto* choice=std::get_if<ConditionalExpr>(&node)) {
      const auto branch=New({FlowEventKind::kTruthBranch,{},{},choice->condition});Link(start,Expr(choice->condition,context.Operand(branch)));
      Link(branch,Expr(choice->consequent,context),FlowCompletion::kTaken);Link(branch,Expr(choice->alternate,context),FlowCompletion::kNotTaken);return;
    }
    if(const auto* logical=std::get_if<LogicalExpr>(&node)) {
      const auto branch=New({logical->operation==TreeLogical::kNullish?FlowEventKind::kNullishBranch:FlowEventKind::kTruthBranch,{},{},logical->left});
      Link(start,Expr(logical->left,context.Operand(branch)));
      const auto right=logical->operation==TreeLogical::kOr?FlowCompletion::kNotTaken:FlowCompletion::kTaken;
      Link(branch,context.next,right==FlowCompletion::kTaken?FlowCompletion::kNotTaken:FlowCompletion::kTaken);
      Link(branch,Expr(logical->right,context),right);return;
    }
    const auto effect=New({FlowEventKind::kEvaluation,{},{},at.id});auto normal=context.next;
    if(!result_.actions.empty())result_.actions[at.id.value]=FlowAction{effect,context.next,{context.thrown,context.finalizer}};
    events_[effect.value].discarded=context.discarded;
    bool required=NormalValueEvaluation(node,result_.normal_kinds,ResourceFailures(tree_.identity.contract))==ExpressionEvaluation::kRequired &&
        projection_.evaluations[at.id.value]!=ExpressionEvaluation::kInert;
    if(const auto* read=std::get_if<SymbolExpr>(&node)) {
      if(!projection_.ReadLiteral(at.id,read->symbol)) {
        events_[effect.value].read=ResolveRead(at.id,read->symbol);events_[effect.value].kind=FlowEventKind::kRead;
        Reference(effect,ResolveRead(at.id,read->symbol),FlowReferenceKind::kRead,at.id,FlowExceptionContext{context.thrown,context.finalizer},context.owner);
      }
    }
    if(const auto* name=std::get_if<GlobalNameExpr>(&node))
      Append(names_,{effect,name->name,at.id,context.owner});
    if(const auto* assignment=std::get_if<AssignmentExpr>(&node)) {
      if(const auto* target=std::get_if<SymbolTarget>(&assignment->target)) {
        const auto copied=CopyValue(assignment->value);
        // A native source declaration initializes a binding at this commit;
        // dead-value elimination must not erase the lexical initialization.
        const auto declaration=initializers_[at.id.value];
        const auto kind=declaration?FlowDefinitionKind::kInitializer:FlowDefinitionKind::kAssignment;
        const bool private_binding=!projection_.private_bindings.empty() &&
            projection_.private_bindings[Resolve(target->symbol).value];
        const bool inert=private_binding && (declaration || projection_.normal_stores[at.id.value]);
        if(declaration && context.discarded)
          discarded_[context.discarded->value].initializer=Resolve(target->symbol);
        if(declaration && context.discarded)
          discarded_[context.discarded->value].evaluation=assignment->value;
        normal=Define(target->symbol,kind,copied,assignment->value,declaration,at.id,
            inert?FlowWriteEffect::kInert:FlowWriteEffect::kObservable,context.discarded,FlowExceptionContext{context.thrown,context.finalizer},context.owner);Link(normal,context.next);
        WriteAction(definitions_.back(),FlowAction{effect,context.next,{context.thrown,context.finalizer}});
        required=!inert;
      }
    }
    if(const auto* update=std::get_if<UpdateExpr>(&node)) {
      normal=Define(update->symbol,FlowDefinitionKind::kUpdate,{},at.id,{},at.id,FlowWriteEffect::kObservable,context.discarded,FlowExceptionContext{context.thrown,context.finalizer},context.owner);Link(normal,context.next);
    }
    // Region effects are composed from semantic operations, independently of
    // whether their result is later used. Inert private writes can be dropped;
    // initialization, getters, coercion, calls, throws and suspension remain.
    events_[effect.value].effect=required?ExpressionEvaluation::kRequired:ExpressionEvaluation::kInert;
    events_[effect.value].effects=required?NormalValueEffects(node,result_.normal_kinds,ResourceFailures(tree_.identity.contract)):SourceEffects{};
    if(context.discarded && required)discarded_[context.discarded->value].effect=ExpressionEvaluation::kRequired;
    const bool suspension=std::holds_alternative<AwaitExpr>(node)||std::holds_alternative<YieldExpr>(node);
    Link(effect,normal,suspension?FlowCompletion::kResumeNext:FlowCompletion::kNormal);
    if(NormalValueEvaluation(node,result_.normal_kinds,ResourceFailures(tree_.identity.contract))==ExpressionEvaluation::kRequired &&
        projection_.evaluations[at.id.value]!=ExpressionEvaluation::kInert &&
        !projection_.normal_stores[at.id.value])Link(effect,context.thrown,suspension?FlowCompletion::kResumeThrow:FlowCompletion::kThrow);
    if(suspension)events_[effect.value].kind=FlowEventKind::kSuspend;
    if(std::holds_alternative<YieldExpr>(node))Abrupt(effect,terminal_,context,FlowCompletion::kResumeReturn);
    // References retain input values once. GetValue precedes call arguments;
    // PutValue follows RHS writes. Receiver/key failures cannot publish access.
    // All motion/liveness/trial consumers see these same phase edges.
    if(const auto* member=std::get_if<MemberExpr>(&node)) {
      auto next=Property(at.id,member->base,member->key,FlowReferenceKind::kRead,effect,context);Link(next,result_.properties.back().receiver);
      if(const auto* key=std::get_if<ComputedKey>(&member->key))next=Expr(key->expression,context.Operand(next));
      Link(start,Expr(member->base,context.Operand(next)));return;
    }
    if(const auto* assignment=std::get_if<AssignmentExpr>(&node))if(const auto* member=std::get_if<MemberTarget>(&assignment->target)) {
      auto next=Property(at.id,member->base,member->key,FlowReferenceKind::kWrite,effect,context);
      Link(next,Expr(assignment->value,context.Operand(result_.properties.back().receiver)));
      if(const auto* key=std::get_if<ComputedKey>(&member->key))next=Expr(key->expression,context.Operand(next));
      Link(start,Expr(member->base,context.Operand(next)));return;
    }
    // Children keep language evaluation order, including callee, receiver and
    // assignment RHS. Their commits are distinct from throwing evaluation.
    std::pmr::vector<ExprId> children(&memory_);
    VisitExpressionChildren(tree_,node,[&](ExprId child){budget_.Step();children.push_back(child);});
    auto next=effect;
    if(const auto* update=std::get_if<UpdateExpr>(&node)) {
      const auto read=New({FlowEventKind::kRead,Resolve(update->symbol),{},at.id});events_[read.value].discarded=context.discarded;Link(read,effect);
      // The update consumes the value captured before conversion. A callback
      // may change the current cell before successful writeback; rereading at
      // the commit would model another operation. Demand and kinds share this
      // input just as they do for copied iteration environments.
      RequireTree(events_[normal.value].definition.has_value(),Error::kOwnership);
      const FlowCapturedValue captured{read,Resolve(update->symbol)};
      definitions_[events_[normal.value].definition->value].input=captured;
      RequireTree(result_.captured_inputs.size()<std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
      events_[effect.value].input=FlowInputId{static_cast<std::uint32_t>(result_.captured_inputs.size())};
      Append(result_.captured_inputs,FlowCapturedInput{effect,captured});
      events_[read.value].effect=ExpressionEvaluation::kRequired;
      events_[read.value].effects=NormalValueEffects(SymbolExpr{update->symbol},{},ResourceFailures(tree_.identity.contract));
      Reference(read,Resolve(update->symbol),FlowReferenceKind::kRead,at.id,FlowExceptionContext{context.thrown,context.finalizer},context.owner);
      Link(read,context.thrown,FlowCompletion::kThrow);next=read;
    }
    for(auto i=children.rbegin();i!=children.rend();++i) {
      // Comma does not observe its left result; only its right transports the
      // parent's value use. All other operands here feed an actual operation.
      auto use=FlowValueUse::kObserved;
      if(const auto* sequence=std::get_if<SequenceExpr>(&node))
        use=*i==sequence->left?FlowValueUse::kDiscarded:context.value_use;
      next=Expr(*i,context.Operand(next,use));
    }
    Link(start,next);
  }
  void Build(Statement at) {
    const auto start=statements_[at.id.value];auto context=at.context;context.owner=at.id;
    current_loop_=context.loop_scope;
    if(native_scopes_&&!projection_.source_scopes.loop_owners.empty())
      if(const auto loop=projection_.source_scopes.loop_owners[at.id.value])current_loop_=*loop;
    if(native_scopes_)if(const auto loop=loop_entries_[at.id.value])current_loop_=*loop;
    context.loop_scope=current_loop_;
    const FlowScopeContext enclosing=context.header?context.header->scope:at.context.owner?statement_scopes_[at.context.owner->value]:FlowScopeContext{IncomingScope{}};
    current_scope_=enclosing;
    if(!context.header && !projection_.omitted[at.id.value])if(const auto* block=std::get_if<BlockStmt>(&tree_.statements[at.id.value].statement))current_scope_=block->scope;
    statement_scopes_[at.id.value]=current_scope_;
    scope_events_[start.value]={enclosing,enclosing};
    if(native_scopes_)loop_scope_events_[start.value]={current_loop_,current_loop_};
    current_owner_=at.id;current_entry_evaluation_=false;events_[start.value].owner=at.id;
    exits_[at.id.value]=context.next;
    const auto projected=[&](const auto& values){return !values.empty()&&bool(values[at.id.value]);};
    if(projected(projection_.initializers)||projected(projection_.forwarded_values)||projected(projection_.loop_declarations))
      events_[start.value].provenance=FlowProvenance::kChanged;
    if(projection_.omitted[at.id.value]){events_[start.value].provenance=FlowProvenance::kChanged;events_[start.value].owner.reset();Link(start,context.next);return;}
    if(const auto loop=loop_entries_[at.id.value]) {
      events_[start.value].provenance=FlowProvenance::kChanged;
      events_[start.value].kind=FlowEventKind::kEnterLoopEnvironment;
      events_[start.value].loop_environment=*loop;
      if(native_scopes_)loop_scope_events_[start.value]={at.context.loop_scope,*loop};
    }
    RequireTree(!result_.statement_semantics[at.id.value],Error::kOwnership);
    result_.statement_semantics[at.id.value]=FlowStatement{at.context.owner};
    Append(result_.statement_order,at.id);
    auto& semantics=*result_.statement_semantics[at.id.value];
    semantics.retention=context.retention;
    semantics.normal_exit=context.next;
    if(!projection_.statement_values.empty())if(const auto value=projection_.statement_values[at.id.value]) {
      events_[start.value].provenance=FlowProvenance::kChanged;
      semantics.entry_value=*value;auto evaluation=context;
      RequireTree(discarded_.size()<std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
      evaluation.discarded=DiscardedId{static_cast<std::uint32_t>(discarded_.size())};Append(discarded_,{at.id});
      evaluation.entry_evaluation=true;evaluation.value_use=FlowValueUse::kDiscarded;
      Link(start,Expr(*value,evaluation));return;
    }
    if(!projection_.declaration_completions.empty())if(const auto& completion=projection_.declaration_completions[at.id.value]) {
      events_[start.value].provenance=FlowProvenance::kChanged;
      const bool initialize=completion->kind==DeclarationCompletionKind::kInitialize;
      std::optional<DiscardedId> id;
      auto evaluation=context;evaluation.value_use=FlowValueUse::kDiscarded;
      if(completion->evaluation) {
        RequireTree(discarded_.size()<std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
        id=DiscardedId{static_cast<std::uint32_t>(discarded_.size())};
        Append(discarded_,{at.id,ExpressionEvaluation::kInert,
            initialize?std::optional{Resolve(completion->binding)}:std::nullopt,completion->evaluation});
        evaluation.discarded=id;
      }
      const auto commit=Define(completion->binding,initialize?FlowDefinitionKind::kUndefined:FlowDefinitionKind::kUndefinedStore,
          {},{},at.id,{},FlowWriteEffect::kInert,id,FlowExceptionContext{context.thrown,context.finalizer},context.owner);
      Link(commit,context.next);evaluation.next=commit;
      Link(start,completion->evaluation?Expr(*completion->evaluation,evaluation):commit);return;
    }
    if(!projection_.declaration_values.empty())if(const auto& value=projection_.declaration_values[at.id.value]) {
          events_[start.value].provenance=FlowProvenance::kChanged;
          RequireTree(discarded_.size()<std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
          const DiscardedId id{static_cast<std::uint32_t>(discarded_.size())};Append(discarded_,{at.id});
          auto evaluation=context;evaluation.discarded=id;evaluation.value_use=FlowValueUse::kDiscarded;
          if(value->target) {
            // The statement discards its result, but the retained assignment
            // consumes the RHS value. Only a later store-elision pass may
            // turn that operand into a discarded value.
            evaluation.value_use=FlowValueUse::kObserved;
            const auto commit=Define(*value->target,FlowDefinitionKind::kAssignment,CopyValue(value->value),
                value->value,at.id,{},FlowWriteEffect::kInert,id,
                FlowExceptionContext{context.thrown,context.finalizer},context.owner);
            evaluation.next=commit;Link(commit,context.next);
          }
          Link(start,Expr(value->value,evaluation));return;
        }
    std::visit([&](const auto& node){
      using T=std::decay_t<decltype(node)>;
      if constexpr(std::is_same_v<T,BlockStmt>) {
        if(context.header) {Link(start,Sequence(at.id,0,node.child_count,context,at.id));return;}
        const auto entered=New({FlowEventKind::kEnterScope,{},{},{},node.scope});
        auto next=New({FlowEventKind::kLeaveScope,{},{},{},node.scope});Link(next,context.next);
        scope_lifetimes_[node.scope.value]=ScopeBindingLifetime{node.scope,entered,next};
        scope_events_[entered.value]={enclosing,node.scope};scope_events_[next.value]={node.scope,enclosing};
        const auto body=loop_bodies_[at.id.value];
        const auto first=body?body->first:0;
        const auto tail=projection_.loops?projection_.loops->tail_starts[at.id.value]:std::nullopt;
        if(body||tail)events_[start.value].provenance=FlowProvenance::kChanged;
        next=Sequence(at.id,first,tail.value_or(node.child_count),context.Operand(next),at.id);
        if(body&&body->retained)next=Stmt(*body->retained,context.Child(next,at.id,FlowStatementRetention::kRequired));
        if(const auto parameter=catch_parameters_[at.id.value]) {
          const auto initialized=Define(*parameter,FlowDefinitionKind::kCatch);
          definitions_.back().catch_entry=start;
          if(result_.original&&!result_.original->catches.empty()) {
            const auto& caught=result_.original->catches[parameter->value];
            if(caught&&caught->body==at.id&&Resolve(*parameter)==*parameter)
              definitions_.back().original_catch=&*caught;
          }
          Link(initialized,next);next=initialized;
        }
        Link(entered,next);Link(start,entered);
      } else if constexpr(std::is_same_v<T,ExpressionStmt>) {
        RequireTree(discarded_.size()<std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
        const DiscardedId id{static_cast<std::uint32_t>(discarded_.size())};
        Append(discarded_,{at.id});
        auto evaluation=context;evaluation.discarded=id;evaluation.value_use=FlowValueUse::kDiscarded;Link(start,Expr(node.expression,evaluation));
      }
      else if constexpr(std::is_same_v<T,DeclarationStmt>) {
        if(!projection_.undefined_stores.empty())if(const auto target=projection_.undefined_stores[at.id.value]) {
          events_[start.value].provenance=FlowProvenance::kChanged;
          const auto commit=Define(*target,FlowDefinitionKind::kUndefinedStore,{},{},at.id,{},FlowWriteEffect::kInert,{},FlowExceptionContext{context.thrown,context.finalizer},context.owner);
          Link(start,commit);Link(commit,context.next);return;
        }
        auto evaluation=context;
        std::optional<DiscardedId> id;
        const bool private_binding=!projection_.private_bindings.empty() && projection_.private_bindings[Resolve(node.symbol).value];
        if(node.initializer) {
          RequireTree(discarded_.size()<std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
          id=DiscardedId{static_cast<std::uint32_t>(discarded_.size())};
          Append(discarded_,{at.id,private_binding?ExpressionEvaluation::kInert:ExpressionEvaluation::kRequired,Resolve(node.symbol),node.initializer});
          evaluation.discarded=id;
        }
        auto commit=Define(node.symbol,node.initializer?FlowDefinitionKind::kInitializer:FlowDefinitionKind::kUndefined,
            node.initializer?CopyValue(*node.initializer):std::nullopt,node.initializer,at.id,{},
            private_binding?FlowWriteEffect::kInert:FlowWriteEffect::kObservable,id,FlowExceptionContext{context.thrown,context.finalizer},context.owner);
        WriteAction(definitions_.back(),FlowAction{commit,context.next,{context.thrown,context.finalizer}});
        if(node.initializer&&!private_binding)events_[commit.value].effect=ExpressionEvaluation::kRequired;
        Link(commit,context.next);
        Link(start,node.initializer?Expr(*node.initializer,evaluation.Operand(commit)):commit);
      } else if constexpr(std::is_same_v<T,IfStmt>) {
        semantics.entry_value=node.condition;auto evaluation=context.Operand(New({FlowEventKind::kTruthBranch,{},{},node.condition}));
        evaluation.entry_evaluation=true;const auto branch=evaluation.next;Link(start,Expr(node.condition,evaluation));
        const auto arm=[&](StmtId child) {
          if(context.header&&context.header->retained==child)return context.next;
          return Stmt(child,context.Child(context.next,at.id,context.header?FlowStatementRetention::kOptional:FlowStatementRetention::kRequired));
        };
        Link(branch,arm(node.consequent),FlowCompletion::kTaken);Link(branch,node.alternate?arm(*node.alternate):context.next,FlowCompletion::kNotTaken);
      } else if constexpr(std::is_same_v<T,LoopStmt>) {
        semantics.progress=FlowStatementProgress::kMayRepeat;semantics.repeat_point=start;
        if(projection_.loops)if(const auto& program=projection_.loops->conditions[at.id.value]) {
          events_[start.value].provenance=FlowProvenance::kChanged;
          // Consume the printer's immutable completion program. Condition and
          // increment execute outside the body activation; the retained arm
          // executes once inside it. Never rediscover an opcode/tree pattern.
          const auto condition=New(),increment=New();
          if(native_scopes_&&loop_members_[at.id.value]) {
            const auto* parent=std::get_if<StmtId>(&context.loop_scope);
            loop_scopes_[at.id.value]=FlowLoopScope{parent?std::optional{*parent}:std::nullopt};
            context.loop_scope=at.id;current_loop_=at.id;
            loop_scope_events_[start.value]={at.id,at.id};
            loop_scope_events_[condition.value]={at.id,at.id};loop_scope_events_[increment.value]={at.id,at.id};
          }
          auto exit=context.next;
          if(native_scopes_&&loop_members_[at.id.value]) {
            FlowEvent leave{FlowEventKind::kLeaveLoopEnvironment};leave.loop_environment=at.id;exit=New(leave);
            loop_scope_events_[exit.value]={at.id,ExitedLoopScope{at.id}};Link(exit,context.next);exits_[at.id.value]=exit;
          }
          const auto entry_copy=IterationCopies(at.id,condition),repeat_copy=IterationCopies(at.id,increment);
          const auto repeat=repeat_copy?repeat_copy->start:increment;
          repeats_[at.id.value]=repeat;semantics.repeat_point=condition;
          loop_bodies_[node.body.value]=LoopBodyProgram{program->header_count,program->body};
          auto update=context.Child(condition,at.id);
          update.header=HeaderProgram{at.id,LoopCompletionTransfer::kContinue,{},condition,enclosing};
          if(const auto& transition=projection_.loops->transitions[at.id.value])
            Link(increment,Sequence(transition->block,transition->first,transition->end,update,at.id));
          else Link(increment,condition);
          const auto body=Stmt(node.body,context.Child(repeat,at.id,FlowStatementRetention::kRequired));
          auto predicate=context.Child(body,at.id);
          predicate.header=HeaderProgram{at.id,LoopCompletionTransfer::kBreak,program->body,exit,enclosing};
          Link(condition,Sequence(node.body,0,program->header_count,predicate,at.id));Link(start,entry_copy?entry_copy->start:condition);
          result_.loops[at.id.value]=FlowLoop{condition,body,increment,exit,
              entry_copy?std::optional{entry_copy->start}:std::nullopt,repeat_copy?std::optional{repeat_copy->start}:std::nullopt,
              entry_copy?std::optional{entry_copy->allocation}:std::nullopt,repeat_copy?std::optional{repeat_copy->allocation}:std::nullopt};return;
        }
        Link(start,Stmt(node.body,context.Child(start,at.id,FlowStatementRetention::kRequired)));
      }
      else if constexpr(std::is_same_v<T,BreakStmt>) {
        semantics.completion=FlowStatementCompletion::kJump;
        if(context.header) {
          RequireTree(context.header->loop==node.target && context.header->transfer==LoopCompletionTransfer::kBreak,Error::kOwnership);
          Abrupt(start,context.header->exit,context,FlowCompletion::kJump);
        } else Abrupt(start,exits_[node.target.value],context,FlowCompletion::kJump);
      }
      else if constexpr(std::is_same_v<T,ContinueStmt>) {
        semantics.completion=FlowStatementCompletion::kJump;
        if(context.header) {
          RequireTree(context.header->loop==node.target && context.header->transfer==LoopCompletionTransfer::kContinue,Error::kOwnership);
          Abrupt(start,context.header->exit,context,FlowCompletion::kJump);
        } else Abrupt(start,repeats_[node.target.value],context,FlowCompletion::kJump);
      }
      else if constexpr(std::is_same_v<T,ReturnStmt>) {
        semantics.completion=FlowStatementCompletion::kAbrupt;
        auto returned=New();Abrupt(returned,terminal_,context,FlowCompletion::kReturn);
        if(!abrupt_actions_.empty())abrupt_actions_[at.id.value]=FlowAbruptAction{returned,terminal_,FlowCompletion::kReturn,{context.thrown,context.finalizer}};
        Link(start,node.value?Expr(*node.value,context.Operand(returned)):returned);
      } else if constexpr(std::is_same_v<T,ThrowStmt>) {
        semantics.completion=FlowStatementCompletion::kAbrupt;
        const auto thrown=New();Link(thrown,context.thrown,FlowCompletion::kThrow);
        events_[thrown.value].thrown_value=node.value;
        if(!abrupt_actions_.empty())abrupt_actions_[at.id.value]=FlowAbruptAction{thrown,context.thrown,FlowCompletion::kThrow,{context.thrown,context.finalizer}};
        Link(start,Expr(node.value,context.Operand(thrown)));
      }
      else if constexpr(std::is_same_v<T,TryStmt>) {
        auto normal=context.next,thrown=context.thrown;auto cleanup=context.finalizer;
        if(node.finalizer) {
          // Conservative cleanup routes do not yet prove exact completion
          // substitution. Publish that capability gap, never guess a shape.
          semantics.completion=FlowStatementCompletion::kCleanup;
          const auto resume=New();Link(resume,context.next);Link(resume,dispatch_,FlowCompletion::kConservativeCleanup);
          cleanup=Stmt(*node.finalizer,context.Child(resume,at.id,FlowStatementRetention::kRequired));normal=*cleanup;thrown=*cleanup;
          Link(dispatch_,*cleanup,FlowCompletion::kConservativeCleanup);Link(dispatch_,context.thrown,FlowCompletion::kConservativeCleanup);
        }
        if(node.handler) {
          auto caught=context.Child(normal,at.id,FlowStatementRetention::kRequired);caught.thrown=thrown;caught.finalizer=cleanup;
          const auto handler=Stmt(node.handler->body,caught);
          // CatchClause creates its environment before InitializeBinding.
          // Publish the initialization inside the handler's activation, so all
          // consumers share the same ordering on every exceptional entry.
          catch_parameters_[node.handler->body.value]=node.handler->parameter;thrown=handler;
        }
        auto protected_body=context.Child(normal,at.id,FlowStatementRetention::kRequired);protected_body.thrown=thrown;protected_body.finalizer=cleanup;
        Link(start,Stmt(node.body,protected_body));
      } else if constexpr(std::is_same_v<T,DebuggerStmt>) {
        events_[start.value].effect=ExpressionEvaluation::kRequired;Link(start,context.next);
      }
      else {static_assert(std::is_same_v<T,UnrecoveredStmt>);throw Error::kUnsupportedSyntax;}
    },tree_.statements[at.id.value].statement);
  }
  FlowScopeContext current_scope_=IncomingScope{};
  FlowLoopContext current_loop_=UnmodeledScope{};
  bool native_scopes_=false;
  std::vector<FlowScopeEvent> scope_events_;
  std::vector<FlowLoopScopeEvent> loop_scope_events_;
  std::vector<std::optional<FlowLoopScope>> loop_scopes_;
  std::optional<StmtId> current_owner_;
  bool current_entry_evaluation_=false;
  const FunctionTree& tree_;LegacyFlowProjection projection_;TreeCheckBudget budget_;RecoveryScratch memory_;
  std::vector<FlowEvent> events_;std::vector<FlowEdge> edges_;
  std::vector<std::optional<FlowAbruptAction>> abrupt_actions_;
  std::pmr::vector<Point> statements_,expressions_,exits_;std::pmr::vector<Task> tasks_;
  std::pmr::vector<std::uint8_t> seen_;std::vector<FlowDefinition> definitions_;
  std::pmr::vector<std::optional<SymbolId>> reads_;std::pmr::vector<ReadState> read_states_;
  std::pmr::vector<std::optional<StmtId>> initializers_;
  std::pmr::vector<std::optional<SymbolId>> catch_parameters_;
  std::pmr::vector<std::optional<ScopeBindingLifetime>> scope_lifetimes_;
  std::pmr::vector<FlowScopeContext> statement_scopes_;
  std::pmr::vector<std::optional<StmtId>> loop_entries_;
  std::pmr::vector<std::optional<NativeLoopAllocation>> native_bindings_;
  std::pmr::vector<std::optional<SymbolId>> loop_members_,member_next_;
  std::pmr::vector<std::optional<LoopBodyProgram>> loop_bodies_;
  std::pmr::vector<Point> repeats_;
  std::vector<DiscardedEvaluation> discarded_;
  std::vector<FlowReference> references_;
  std::vector<FlowNameReference> names_;
  Point entry_{},terminal_{},dispatch_{};
  RecoveryFlow result_;
};
}
std::optional<FlowPoint> FindOriginalPhase(const RecoveryFlow& flow,analysis::WorkBlockId block,
    analysis::ExceptionPhase phase,std::optional<analysis::OpId> operation) {
  if(!flow.original||block.value>=flow.original->phases.size()||block.value>=flow.original_phases.size())return {};
  const auto& original=flow.original->phases[block.value];
  if(original.phase!=phase||original.operation!=operation)return {};
  return flow.original_phases[block.value];
}
std::optional<OriginalResult> FindFlowOriginalResult(const RecoveryFlow& flow,analysis::OpId operation) {
  if(!flow.original||operation.value>=flow.original_results.size()||!flow.original_results[operation.value])return {};
  const auto result=FindOriginalResult(*flow.original,operation);if(!result)return {};
  const auto& occurrence=*flow.original_results[operation.value];
  if(occurrence.expression!=result->expression||result->expression.value>=flow.expressions.size())return {};
  if(result->commit&&FindOriginalPhase(flow,*result->commit,analysis::ExceptionPhase::kCommit,operation)!=occurrence.normal)return {};
  return result;
}
namespace {
FlowOriginalProofState OriginalIncomingState(const RecoveryFlow& flow,const analysis::IncomingKey& incoming,std::span<const TruthDomains> truths) {
  using State=FlowOriginalProofState;
  // Consume the verified decision-owner correspondence. This withdraws an
  // incoming certificate when its controlling input has no current occurrence;
  // source definition placement and the joint completion/provenance solver
  // still own path, execution-count and operand-identity obligations. An
  // unconditional incoming edge is not a second control-shape admission rule.
  const auto* edge=std::get_if<analysis::EdgeId>(&incoming);if(!edge)return State::kAvailable;
  if(!flow.original||edge->value>=flow.original->edges.size()||
      flow.original->edge_controls.size()!=flow.original->edges.size())return State::kUnavailable;
  const auto owner=flow.original->edge_controls[edge->value];
  if(!owner)return State::kAvailable;
  const auto control=FindFlowOriginalControl(flow,*owner);if(!control)return State::kUnavailable;
  // Unmodeled tests retain the coarse input-identity contract. A proved pure
  // component instead consumes its qualified input/path domain. Source guard,
  // occurrence count, completion and provenance remain joint-solver duties.
  if(!control->test)return State::kAvailable;
  if(truths.empty())truths=flow.normal_truths;
  if(control->expression.value>=truths.size())return State::kUnavailable;
  if(truths[control->expression.value].Empty())return State::kPending;
  const auto domains=OriginalControlDomains(flow,*owner,truths);if(!domains)return State::kUnavailable;
  switch(flow.original->edges[edge->value].kind) {
    case analysis::FlowEdgeKind::kTaken:return domains->true_values.Empty()?State::kUnreachable:State::kAvailable;
    case analysis::FlowEdgeKind::kFallthrough:return domains->false_values.Empty()?State::kUnreachable:State::kAvailable;
    default:return State::kAvailable;
  }
}
bool OriginalHandlerAvailable(const RecoveryFlow& flow,analysis::WorkBlockId handler) {
  return FindOriginalPhase(flow,handler,analysis::ExceptionPhase::kCatch)&&handler.value<flow.original_handlers.size()&&
      flow.original_handlers[handler.value]==FlowOriginalRouteCoverage::kComplete;
}
}
std::optional<FlowOriginalControl> FindFlowOriginalControl(const RecoveryFlow& flow,analysis::OpId id) {
  if(!flow.original||id.value>=flow.original->operations.size()||id.value>=flow.original_controls.size())return {};
  const auto& operation=flow.original->operations[id.value];const auto& occurrence=flow.original_controls[id.value];
  if(operation.life!=analysis::ValueLife::kLive||!operation.control||!occurrence||
      occurrence->expression!=operation.control->expression||occurrence->kind!=operation.control->kind||occurrence->test!=operation.control->test||
      occurrence->expression.value>=flow.expressions.size()||occurrence->expression.value>=flow.normal_exits.size()||
      flow.normal_exits[occurrence->expression.value]!=occurrence->normal||
      flow.expressions[occurrence->expression.value]!=occurrence->entry)return {};
  return occurrence;
}
std::optional<FlowControlDomains> OriginalControlDomains(const RecoveryFlow& flow,analysis::OpId id,std::span<const TruthDomains> truths) {
  const auto control=FindFlowOriginalControl(flow,id);if(!control||!control->test)return {};
  if(truths.empty())truths=flow.normal_truths;
  if(control->expression.value>=truths.size())return {};
  const auto& test=*control->test;const auto evaluated=truths[control->expression.value]&test.evaluated;
  return FlowControlDomains{evaluated,evaluated&test.true_values,evaluated&test.true_values.Complement()};
}
NormalValueDomain OriginalControlResultDomains(const RecoveryFlow& flow,analysis::OpId id,std::span<const TruthDomains> truths) {
  const auto control=FindFlowOriginalControl(flow,id);
  if(!control||!control->test)return {};
  const auto domains=OriginalControlDomains(flow,id,truths);if(!domains||domains->evaluated.Empty())return {};
  switch(control->test->outcome) {
    case PureTruthOutcome::kDecision:return {};
    case PureTruthOutcome::kInput:
      if(control->expression.value>=flow.normal_kinds.size())return {};
      return {flow.normal_kinds[control->expression.value],domains->evaluated};
    case PureTruthOutcome::kBoolean: {
      TruthDomains result;
      if(!domains->true_values.Empty())result|=TruthDomains::Truthy();
      if(!domains->false_values.Empty())result|=TruthDomains::One(TruthDomain::kFalsy);
      return {analysis::ValueKinds::Boolean(),result};
    }
  }
  throw TreeStructureError::kUnsupportedSyntax;
}
std::optional<analysis::OpId> OriginalDefinitionControl(const RecoveryFlow& flow,const OriginalSourceDefinition& definition) {
  const analysis::IncomingKey* incoming=nullptr;
  if(const auto* phi=std::get_if<OriginalPhiDefinition>(&definition.origin))incoming=&phi->incoming;
  if(const auto* saved=std::get_if<OriginalSavedDefinition>(&definition.origin))incoming=std::get_if<analysis::IncomingKey>(&saved->placement);
  const auto* edge=incoming?std::get_if<analysis::EdgeId>(incoming):nullptr;
  if(!edge||!flow.original||edge->value>=flow.original->edge_controls.size())return {};
  return flow.original->edge_controls[edge->value];
}
FlowOriginalDefinitionProof OriginalDefinitionProof(const RecoveryFlow& flow,const OriginalSourceDefinition& definition,std::span<const TruthDomains> truths) {
  using State=FlowOriginalProofState;
  if(!flow.original)return {};
  if(const auto* result=std::get_if<OriginalResultDefinition>(&definition.origin)) {
    const auto verified=FindFlowOriginalResult(flow,result->operation);
    return verified&&verified->value==result->value&&verified->expression==definition.input?
        FlowOriginalDefinitionProof{State::kAvailable,verified->kinds}:FlowOriginalDefinitionProof{};
  }
  if(const auto* caught=std::get_if<OriginalCatchDefinition>(&definition.origin))
    if(!OriginalHandlerAvailable(flow,caught->handler))return {};
  if(const auto* phi=std::get_if<OriginalPhiDefinition>(&definition.origin))
    if(const auto state=OriginalIncomingState(flow,phi->incoming,truths);state!=State::kAvailable)return {state,{}};
  if(const auto* saved=std::get_if<OriginalSavedDefinition>(&definition.origin))
    if(const auto* incoming=std::get_if<analysis::IncomingKey>(&saved->placement))
      if(const auto state=OriginalIncomingState(flow,*incoming,truths);state!=State::kAvailable)return {state,{}};
  return {State::kAvailable,OriginalDefinitionKinds(*flow.original,definition)};
}
analysis::ValueKinds OriginalDefinitionKinds(const RecoveryFlow& flow,const OriginalSourceDefinition& definition,std::span<const TruthDomains> truths) {
  return OriginalDefinitionProof(flow,definition,truths).kinds;
}
analysis::ValueKinds OriginalCatchKinds(const RecoveryFlow& flow,const OriginalCatchBinding& caught) {
  if(!OriginalHandlerAvailable(flow,caught.handler))return {};
  RequireTree(caught.value.value<flow.original->values.size(),Error::kOwnership);
  return flow.original->values[caught.value.value].domain;
}
std::variant<RecoveryFlow,TreeStructureError> BuildRecoveryFlow(
    const FunctionTree& tree,LegacyFlowProjection projection,analysis::SemanticBudget budget,RecoveryFlow storage) {
  try{return FlowBuilder(tree,projection,budget,std::move(storage)).Run();}
  catch(Error error){return error;}catch(const std::bad_alloc&){return Error::kResourceLimit;}
  catch(const std::length_error&){return Error::kResourceLimit;}
}
FlowEventEffects EvaluateFlowEventEffects(const RecoveryFlow& flow,const FunctionTree& tree,
    const FlowEvent& event,FlowEffectInput input) {
  if(event.definition) {
    RequireTree(event.definition->value<flow.definitions.size(),Error::kOwnership);
    // Successful writes own retention separately from computing their value.
    // Refining conversion/callback effects cannot erase an observable commit;
    // private-write deletion remains the demand/dead-definition analysis's job.
    return {flow.definitions[event.definition->value].effect==FlowWriteEffect::kObservable?
        ExpressionEvaluation::kRequired:ExpressionEvaluation::kInert,event.effects};
  }
  if(!event.expression||event.kind==FlowEventKind::kTruthBranch||event.kind==FlowEventKind::kNullishBranch)
    return {event.effect,event.effects};
  const auto kinds=input.normal_kinds.empty()?std::span<const analysis::ValueKinds>(flow.normal_kinds):input.normal_kinds;
  const auto resources=ResourceFailures(tree.identity.contract);
  if(event.kind==FlowEventKind::kPropertyReference) {
    RequireTree(event.property&&event.property->reference.value<flow.properties.size(),Error::kOwnership);
    const auto& reference=flow.properties[event.property->reference.value];
    SourceEffects effects;
    switch(event.property->phase) {
      case FlowPropertyPhase::kAcquisition:break;
      case FlowPropertyPhase::kReceiver:effects=NormalPropertyReceiverEffects(reference.base,kinds,resources);break;
      case FlowPropertyPhase::kKey:effects=NormalPropertyKeyEffects(reference.key,kinds,resources);break;
      case FlowPropertyPhase::kAccess:throw Error::kOwnership;
    }
    return {effects.Evaluation(),effects};
  }
  const auto id=*event.expression;const auto& node=tree.expressions[id.value].expression;
  auto captured=analysis::ValueKinds::Language();
  if(event.kind==FlowEventKind::kEvaluation&&event.input) {
    RequireTree(event.input->value<flow.captured_inputs.size()&&
        (input.input_kinds.empty()||input.input_kinds.size()==flow.captured_inputs.size()),Error::kOwnership);
    captured=input.input_kinds.empty()?flow.captured_inputs[event.input->value].kinds:input.input_kinds[event.input->value];
  }
  const auto intrinsic=NormalValueEffects(node,kinds,resources,captured);
  bool required=intrinsic.Evaluation()==ExpressionEvaluation::kRequired&&
      input.evaluations[id.value]!=ExpressionEvaluation::kInert;
  if(std::holds_alternative<AssignmentExpr>(node))required=input.assignments[id.value]==ExpressionEvaluation::kRequired;
  auto effects=required?intrinsic:SourceEffects{};
  if(std::holds_alternative<AssignmentExpr>(node)&&input.normal_stores[id.value]) {
    // Successful lexical PutValue is independent of observable write
    // retention and of the RHS's separately evaluated throwing phases.
    effects.before_throw=analysis::LanguageEffect::kNone;
    effects.exceptional=analysis::ExceptionalCompletion::kImpossible;
  }
  if(event.kind==FlowEventKind::kRead&&std::holds_alternative<UpdateExpr>(node))
    effects=NormalValueEffects(SymbolExpr{std::get<UpdateExpr>(node).symbol},{},resources);
  if(event.kind==FlowEventKind::kRead&&event.read_completion==FlowReadCompletion::kInitialized) {
    required=false;effects=SourceEffects{};
  }
  if(event.original_effect_valid&&event.kind==FlowEventKind::kEvaluation&&!flow.original_effects.empty()) {
    RequireTree(id.value<flow.original_effects.size(),Error::kOwnership);
    if(const auto& proof=flow.original_effects[id.value]) {
      effects=IntersectSourceEffects(effects,*proof);required=effects.Evaluation()==ExpressionEvaluation::kRequired;
    }
  }
  return {required?ExpressionEvaluation::kRequired:ExpressionEvaluation::kInert,effects};
}
std::variant<FlowRefinement,TreeStructureError> RefineRecoveryFlowEffects(
    RecoveryFlow& flow,const FunctionTree& tree,std::span<const ExpressionEvaluation> evaluations,
    std::span<const std::uint8_t> normal_stores,analysis::SemanticBudget allowance,
    std::span<const std::uint8_t> executable) {
  try {
    TreeCheckBudget budget(allowance);
    RequireTree(executable.empty()||executable.size()==flow.edges.size(),TreeStructureError::kOwnership);
    std::vector<ExpressionEvaluation> assignments;
    budget.Array(assignments,tree.expressions.size());
    for(auto& effect:assignments){budget.Step();effect=ExpressionEvaluation::kRequired;}
    for(auto& definition:flow.definitions) {
      budget.Step();if(!definition.write || (definition.kind!=FlowDefinitionKind::kAssignment &&
          definition.kind!=FlowDefinitionKind::kInitializer))continue;
      const bool inert=flow.bindings[definition.binding.value].exposure==FlowBindingExposure::kPrivate &&
          (definition.kind==FlowDefinitionKind::kInitializer || normal_stores[definition.write->value]);
      definition.effect=inert?FlowWriteEffect::kInert:FlowWriteEffect::kObservable;
      assignments[definition.write->value]=inert?
          ExpressionEvaluation::kInert:ExpressionEvaluation::kRequired;
    }
    for(auto& discarded:flow.discarded){budget.Step();discarded.effect=ExpressionEvaluation::kInert;}
    const FlowEffectInput input{evaluations,normal_stores,assignments};
    for(auto& event:flow.events) {
      budget.Step();const auto effects=EvaluateFlowEventEffects(flow,tree,event,input);
      event.effect=effects.retention;event.effects=effects.intrinsic;
      event.incoming.reset();event.outgoing.reset();
      if(event.discarded&&event.effect==ExpressionEvaluation::kRequired)
        flow.discarded[event.discarded->value].effect=ExpressionEvaluation::kRequired;
    }
    std::size_t retained=0;
    for(std::size_t i=0;i<flow.edges.size();++i) {
      budget.Step();auto edge=flow.edges[i];const auto& event=flow.events[edge.from.value];
      if(!executable.empty()&&!executable[i])continue;
      if(edge.completion==FlowCompletion::kThrow && event.expression) {
        // Intrinsic completion belongs to this phase, not its enclosing
        // expression's aggregate effects. Child/coercion failures keep their
        // own edges; an initialized lexical read cannot throw a TDZ error.
        if(event.effects&&event.effects->exceptional==analysis::ExceptionalCompletion::kImpossible)continue;
      }
      edge.incoming=flow.events[edge.to.value].incoming;edge.outgoing=flow.events[edge.from.value].outgoing;
      flow.edges[retained]=edge;flow.events[edge.to.value].incoming=retained;
      flow.events[edge.from.value].outgoing=retained;++retained;
    }
    flow.edges.resize(retained);return FlowRefinement{budget.Bytes(),budget.Work()};
  }catch(Error error){return error;}catch(const std::bad_alloc&){return Error::kResourceLimit;}
  catch(const std::length_error&){return Error::kResourceLimit;}
}
}
