#include "tree_flow.h"

#include <algorithm>
#include <new>
#include <stdexcept>

#include "class_inner_scope.h"
#include "cell_lifetime.h"
#include "completion_tree.h"
#include "concurrent_tree.h"
#include "exception_tree.h"
#include "for_in_emission.h"
#include "for_of_evaluation.h"
#include "logical_evaluation.h"
#include "logical_regions.h"
#include "literal_field_forest.h"
#include "tree_checks.h"
#include "switch_region.h"
#include "switch_syntax.h"
#include "tree_edge_flow.h"

namespace abcd::source {
namespace {
using Error = TreeFlowError;
void Require(bool condition, Error error) { if (!condition) throw error; }
struct FlowFailure { TreeFlowError kind; TreeLocation location; };
struct TreeRegion {
  ControlId entry;
  StmtId block;
  std::uint32_t first_child = 0;
};
class FlowCheck {
  std::vector<std::optional<std::uint32_t>> switch_entries_, switch_tests_;
  std::vector<std::optional<StmtId>> switch_statements_;
  mutable TreeLocation location_;
  struct LocationScope {
    const FlowCheck& owner; TreeLocation previous;
    LocationScope(const FlowCheck& o, TreeLocation location) : owner(o), previous(o.location_) { o.location_ = location; }
    ~LocationScope() { owner.location_ = previous; }
  };
  TreeLocation At(analysis::OpId id) const { return OperationLocation(graph_, id, tree_.method); }
  void Require(bool condition, Error error) const {
    if (!condition) throw FlowFailure{error, location_};
  }
 public:
  FlowCheck(const analysis::ValueProgram& graph, const ControlPlan& control,
      const EvaluationPlan& evaluation, const FunctionTree& tree,
      TreeCheckBudget& budget, const CompletionTreeView& sequences,
      std::span<const ScopeId> expression_scopes,
      const ExceptionTreeInput* exceptions = nullptr)
      : graph_(graph), control_(control), evaluation_(evaluation), tree_(tree), budget_(budget),
        exceptions_(exceptions), sequences_(sequences), expression_scopes_(expression_scopes) {
    budget.Array(placements_, graph.ops.size());
    budget.Array(flow_.expressions, graph.ops.size());
    budget.Array(flow_.statements, graph.ops.size());
    budget.Array(flow_.initializers, graph.ops.size());
    budget.Array(flow_.buffered_properties, graph.ops.size());
    budget.Array(flow_.defined_properties, tree.properties.size());
    budget.Array(flow_.snapshots, graph.ops.size());
    budget.Array(flow_.definitions, tree.expressions.size());
    budget.Array(flow_.declarations, tree.statements.size());
    budget.Array(flow_.receivers, graph.ops.size());
    if (evaluation.cells) {
      budget.Array(flow_.cells, evaluation.cells->cells.cells.size());
      budget.Array(flow_.cell_bindings, evaluation.cells->cells.cells.size());
    }
    budget.Array(assigned_, tree.symbols.size());
    budget.Array(declared_, tree.symbols.size());
    budget.Array(written_, tree.symbols.size());
    // Source parameter writes would invalidate an entry-bound SSA value.
    // One census covers all assignment/update occurrences, independent of CFG
    // layout. Rebinding cells is handled by their distinct storage as before.
    for (const auto& expression : tree.expressions) {
      budget.Step();
      if (const auto* assignment = std::get_if<AssignmentExpr>(&expression.expression))
        if (const auto* target = std::get_if<SymbolTarget>(&assignment->target))
          written_[target->symbol.value] = 1;
      if (const auto* update = std::get_if<UpdateExpr>(&expression.expression))
        written_[update->symbol.value] = 1;
    }
    budget.Array(tasks_, control.nodes.size());
    budget.Array(loops_, control.loops.size());
    if (exceptions) {
      budget.Array(flow_.catch_parameters, graph.values.size());
      budget.Array(flow_.tries, exceptions->regions.regions.size());
      std::size_t inactive = 0;
      for (const auto& r : exceptions->regions.layout.regions) { budget.Step(); inactive += r.inactive.has_value(); }
      if (inactive || (exceptions->for_of && !exceptions->for_of->ownership.Forwarding().regions.empty())) {
        budget.Array(flow_.exception_regions,exceptions->regions.layout.regions.size());
        for (std::size_t r=0;r<flow_.exception_regions.size();++r) {
          budget.Step();const auto& input=exceptions->regions.layout.regions[r];
          flow_.exception_regions[r]={input.body,input.handler};
        }
      }
      budget.Array(flow_.inactive_regions, inactive);
      std::size_t index = 0;
      for (const auto& r : exceptions->regions.layout.regions) {
        budget.Step(); if (r.inactive) flow_.inactive_regions[index++] = *r.inactive;
      }
    }
    budget.Array(flow_.branch_polarity, graph.ops.size());
    budget.Array(flow_.edge_controls,graph.edges.size());
    for(const auto& node:control.nodes) {
      budget.Step();if(!node.arms||!std::holds_alternative<std::monostate>(node.synthetic))continue;
      const auto predicate=DecisionPredicate(node);const auto& block=DecisionBlock(node);
      for(std::uint8_t i=0;i<block.edge_count;++i) {
        budget.Step();const analysis::EdgeId edge{block.first_edge.value+i};
        Require(edge.value<graph.edges.size()&&!flow_.edge_controls[edge.value],Error::kControl);
        flow_.edge_controls[edge.value]=predicate;
      }
    }
    budget.Array(decision_branches_, graph.ops.size());
    for (const auto& node : control.nodes) {
      budget.Step(); if (!node.decision) continue;
      const auto predicate = node.decision->predicate.value_or(node.decision->evaluate);
      decision_branches_[DecisionPredicate(control.nodes[predicate.value]).value] = 1;
    }
    budget.Array(flow_.controls, control.nodes.size() -
        (exceptions ? exceptions->control.units.size() : graph.blocks.size()));
    flow_.first_control = {static_cast<std::uint32_t>(control.nodes.size() - flow_.controls.size())};
    budget.Array(flow_.control_statements, flow_.controls.size());
    if (!control.forwarding.empty()) budget.Array(flow_.forwarded_controls, flow_.controls.size());
    if(control.switches) {
      budget.Array(switch_entries_,graph.blocks.size());budget.Array(switch_tests_,graph.blocks.size());
      budget.Array(switch_statements_,control.switches->regions.size());budget.Array(flow_.switch_controls,flow_.controls.size());
      for(std::uint32_t p=0;p<control.switches->ports.size();++p){budget.Step();const auto kind=control.switches->ports[p].kind;if(kind==SwitchPortKind::kMatch||kind==SwitchPortKind::kMiss)flow_.switch_controls[p]=1;}
      for(std::uint32_t r=0;r<control.switches->regions.size();++r){budget.Step();switch_entries_[control.switches->regions[r].entry.value]=r;}
      for(std::uint32_t c=0;c<control.switches->clauses.size();++c)if(const auto&t=control.switches->clauses[c].test){budget.Step();switch_tests_[t->block.value]=c;decision_branches_[t->branch.value]=1;}
    }
    if (evaluation.phis) {
      logicals_ = FindSelectionExpressions(graph, control, evaluation, budget,
          exceptions ? &exceptions->control : nullptr);
      if (!logicals_.empty()) {
        budget.Array(logical_branches_, graph.ops.size());
        budget.Array(flow_.logical_operations, graph.ops.size());
        budget.Array(flow_.selection_truths, graph.ops.size());
        budget.Array(flow_.conditional_operations, graph.ops.size());
        budget.Array(flow_.branch_predicates, graph.ops.size());
        budget.Array(conditional_tasks_, logicals_.size());
        budget.Array(original_prefixes_, graph.ops.size());
        for (std::size_t i = 0; i < logicals_.size(); ++i) {
          logical_branches_[logicals_[i].branch.value] = i;
          if (logicals_[i].original_prefix) for (const auto prefix : *logicals_[i].original_prefix) {
            budget.Step(); Require(!original_prefixes_[prefix.value], Error::kPlacement); original_prefixes_[prefix.value] = 1;
          }
        }
      }
    }
    edges_.emplace(graph, evaluation, tree, flow_, declared_, assigned_, budget, sequences);
    for (const auto symbol : sequences.Symbols()) {
      Require(!declared_[symbol.value], Error::kPlacement);
      declared_[symbol.value] = 1;
      assigned_[symbol.value] = tree.symbols[symbol.value].kind != BindingKind::kCatchParameter;
    }
  }
  TreeFlow Run() {
    if (evaluation_.cells && !evaluation_.cells->deferred.empty()) {
      // Recheck successful Commit dominance on the actual normal/exception
      // graph. Native declaration scope is an additional independent proof.
      const auto checked = VerifyDeferredCells(graph_, *evaluation_.cells, budget_.Remaining());
      Require(checked.has_value(), Error::kEvaluation);
      budget_.Step(*checked);
    }
    if(evaluation_.for_of) {
      Require(exceptions_ && exceptions_->for_of,Error::kControl);
      auto view=BuildForOfEmissionView(graph_,*exceptions_->for_of,*evaluation_.for_of,budget_.Remaining());
      if(const auto* f=std::get_if<ForOfIterationFailure>(&view)) {
        if(f->kind==ForOfIterationError::kResourceLimit)throw Error::kResourceLimit;
        if(f->kind==ForOfIterationError::kWorkLimit)throw Error::kWorkLimit;
        throw Error::kControl;
      }
      iteration_=std::get<ForOfEmissionView>(std::move(view));budget_.Admit(iteration_->allocated_bytes,1);budget_.Step(iteration_->work);
      budget_.Array(iteration_statements_,iteration_->loops.size());budget_.Array(iteration_scopes_,iteration_->loops.size());
      flow_.for_of.emplace();budget_.Array(flow_.for_of->statements,iteration_->loops.size());budget_.Array(flow_.for_of->closes,iteration_->closes.size());
      budget_.Array(flow_.for_of->controls,flow_.controls.size());
      for(std::size_t n=flow_.first_control.value;n<control_.nodes.size();++n) {
        budget_.Step();const auto& node=control_.nodes[n];
        if(const auto* wrapper=std::get_if<LoopControl>(&node.synthetic)) {
          if(iteration_->wrappers[wrapper->loop.value])flow_.for_of->controls[n-flow_.first_control.value]=EnumerationControl::kLoop;
        } else if(const auto* transfer=std::get_if<LoopTransfer>(&node.synthetic)) {
          const auto native=iteration_->wrappers[transfer->loop.value];const auto* edge=std::get_if<analysis::EdgeId>(&transfer->source);
          if(native && edge && transfer->kind==LoopTransferKind::kBreak && exceptions_->control.arcs[edge->value].edge==iteration_->loops[*native].exhausted)
            flow_.for_of->controls[n-flow_.first_control.value]=EnumerationControl::kExhausted;
        }
      }
    }
    if (exceptions_ && evaluation_.cells && !evaluation_.cells->deferred.empty()) {
      Require(evaluation_.cells->environments != nullptr, Error::kEvaluation);
      auto scopes = BuildPhasedCellLifetimes(graph_, *evaluation_.cells->environments,
          evaluation_.cells->cells, *exceptions_, budget_.Remaining(), evaluation_.for_of ? &*evaluation_.for_of : nullptr,
          iteration_ ? &*iteration_ : nullptr,
          evaluation_.cells->deferred);
      if (const auto* error = std::get_if<CellLifetimeError>(&scopes)) {
        if (*error == CellLifetimeError::kResourceLimit) throw Error::kResourceLimit;
        if (*error == CellLifetimeError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kPlacement;
      }
      deferred_scopes_.emplace(std::get<CellLifetimes>(std::move(scopes)));
      budget_.Admit(deferred_scopes_->allocated_bytes, 1); budget_.Step(deferred_scopes_->work);
      budget_.Array(deferred_scope_blocks_, deferred_scopes_->scopes.size());
    }
    if (evaluation_.for_in) {
      Require(exceptions_, Error::kControl);
      auto view = BuildForInEmissionView(graph_, exceptions_->regions, exceptions_->control,
          *evaluation_.for_in, *evaluation_.for_in_events, budget_.Remaining(),
          iteration_ ? &iteration_->forwarding : nullptr);
      if (const auto* failure = std::get_if<ForInFailure>(&view)) {
        if (failure->kind == ForInError::kResourceLimit) throw Error::kResourceLimit;
        if (failure->kind == ForInError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kControl;
      }
      enumeration_ = std::get<ForInEmissionView>(std::move(view));
      budget_.Admit(enumeration_->allocated_bytes, 1); budget_.Step(enumeration_->work);
      const auto copies = VerifyForInCopyState(graph_, evaluation_, *enumeration_, budget_.Remaining());
      if (const auto* failure = std::get_if<ForInFailure>(&copies)) {
        if (failure->kind == ForInError::kResourceLimit) throw Error::kResourceLimit;
        if (failure->kind == ForInError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kControl;
      }
      budget_.Step(std::get<std::uint64_t>(copies));
      budget_.Array(enumeration_statements_, enumeration_->loops.size());
      budget_.Array(enumeration_scopes_, enumeration_->loops.size());
      budget_.Array(flow_.enumerations, enumeration_->loops.size());
      budget_.Array(flow_.enumeration_controls,flow_.controls.size());
      for(std::size_t n=flow_.first_control.value;n<control_.nodes.size();++n) {
        budget_.Step(); const auto& node=control_.nodes[n];
        if(const auto* loop=std::get_if<LoopControl>(&node.synthetic)) {
          if(enumeration_->wrappers[loop->loop.value])flow_.enumeration_controls[n-flow_.first_control.value]=EnumerationControl::kLoop;
        } else if(const auto* transfer=std::get_if<LoopTransfer>(&node.synthetic)) {
          const auto native=enumeration_->wrappers[transfer->loop.value];
          const auto* edge=std::get_if<analysis::EdgeId>(&transfer->source);
          if(native && edge && transfer->kind==LoopTransferKind::kBreak &&
              exceptions_->control.arcs[edge->value].edge==evaluation_.for_in->loops[*native].done_edge)
            flow_.enumeration_controls[n-flow_.first_control.value]=EnumerationControl::kExhausted;
        }
      }
    }
    IndexPlacements();
    ModulePairs();
    CheckBodyOrigin();
    CapturedCells();
    ModuleAliases();
    auto first_child = Declarations();
    PredicateSnapshots();
    edges_->Consume(analysis::MethodEntryKey{},
        sequences_.Block(tree_.body), first_child);
    std::size_t pending = 1;
    tasks_[0] = {control_.entry, tree_.body, first_child};
    while (pending != 0) {
      budget_.Step();
      auto region = tasks_[--pending];
      const auto& block = sequences_.Block(region.block);
      auto child = region.first_child;
      std::optional<ControlId> cursor = region.entry;
      while (cursor) {
        budget_.Step();
        const auto& node = control_.nodes[cursor->value];
        if (exceptions_ && std::holds_alternative<std::monostate>(node.synthetic)) {
          const auto& unit = exceptions_->control.units[cursor->value];
          if(const auto* close=std::get_if<ProtocolCloseUnit>(&unit)) {
            Require(iteration_ && iteration_->closes[cursor->value],Error::kControl);
            if(close->pending.kind==analysis::PendingCompletionKind::kReturn || close->pending.kind==analysis::PendingCompletionKind::kThrow) {
              Require(child<block.child_count,Error::kSequence);const auto statement=sequences_.Child(block.first_child+child++);
              const auto& syntax=tree_.statements[statement.value].statement;
              std::optional<ExprId> value;
              if(close->pending.kind==analysis::PendingCompletionKind::kReturn){const auto* returned=std::get_if<ReturnStmt>(&syntax);Require(returned,Error::kPlacement);value=returned->value;}
              else {const auto* thrown=std::get_if<ThrowStmt>(&syntax);Require(thrown,Error::kPlacement);value=thrown->value;}
              Require(value && iteration_->closes[cursor->value]->payload,Error::kPlacement);
              IterationPayload(*iteration_->closes[cursor->value]->payload,*value);
              flow_.for_of->closes[cursor->value]=statement;
            }
            cursor=node.next;continue;
          }
          if (const auto* region = std::get_if<TryControlUnit>(&unit)) {
            if (enumeration_ && enumeration_->forwarded_regions[region->region.value]) {
              cursor = enumeration_->continuations[cursor->value]->next; continue;
            }
            Try(region->region, block, child, pending);
            cursor = node.next;
            continue;
          }
          if (std::holds_alternative<TryExitUnit>(unit)) {
            cursor = enumeration_ && enumeration_->continuations[cursor->value] ? enumeration_->continuations[cursor->value]->next : node.next;
            continue;
          }
          const auto original = std::get<PhaseControlUnit>(unit).block;
          Require(SourceBlock(node) == original, Error::kControl);
          if (graph_.phases[original.value].phase == analysis::ExceptionPhase::kCatch)
            edges_->Catch(original, block, child);
        }
        if (const auto* port = std::get_if<ControlForward>(&node.synthetic)) {
          Require(!exceptions_ && cursor->value >= flow_.first_control.value, Error::kControl);
          flow_.forwarded_controls[cursor->value - flow_.first_control.value] = *port;
          cursor = node.next; continue;
        }
        if(const auto* port=std::get_if<SwitchPort>(&node.synthetic)) {
          Require(control_.switches && port->port<control_.switches->ports.size(),Error::kControl);
          const auto& evidence=control_.switches->ports[port->port];
          const auto offset=cursor->value-flow_.first_control.value;
          if(evidence.kind==SwitchPortKind::kBreak) {
            Require(child<block.child_count && switch_statements_[evidence.region],Error::kSequence);
            const auto statement=sequences_.Child(block.first_child+child++);
            const auto* jump=std::get_if<BreakStmt>(&tree_.statements[statement.value].statement);
            const auto origin=tree_.statements[statement.value].origin;
            Require(jump && jump->target==*switch_statements_[evidence.region] && origin.value<tree_.origins.size(),Error::kPlacement);
            const auto* edge=std::get_if<EdgeOrigin>(&tree_.origins[origin.value]);Require(edge && edge->edge==evidence.edge,Error::kPlacement);
            flow_.controls[offset]=origin;flow_.control_statements[offset]=statement;
          }else {Require(evidence.kind==SwitchPortKind::kFallThrough,Error::kControl);flow_.switch_controls[offset]=1;}
          cursor=node.next;continue;
        }
        if (!std::holds_alternative<std::monostate>(node.synthetic)) {
          if(iteration_)if(const auto* wrapper=std::get_if<LoopControl>(&node.synthetic);wrapper && iteration_->wrappers[wrapper->loop.value]) {
            const auto i=*iteration_->wrappers[wrapper->loop.value];Require(iteration_statements_[i] && loops_[wrapper->loop.value],Error::kControl);
            const auto statement=*iteration_statements_[i];const auto* syntax=std::get_if<ForOfStmt>(&tree_.statements[statement.value].statement);
            Require(syntax && child<block.child_count && sequences_.Child(block.first_child+child++)==statement && iteration_scopes_[i]==block.scope,Error::kSequence);
            Require(iteration_->loops[i].exhaustion_only || !edges_->HasCopies(iteration_->loops[i].exhausted),Error::kSequence);
            edges_->Consume(iteration_->loops[i].exhausted,block,child);
            const auto offset=cursor->value-flow_.first_control.value;flow_.controls[offset]=tree_.statements[statement.value].origin;flow_.control_statements[offset]=statement;
            Require(pending<tasks_.size(),Error::kResourceLimit);tasks_[pending++]={control_.loops[wrapper->loop.value].body,syntax->body,0};cursor=node.next;continue;
          }
          if (enumeration_) if (const auto* wrapper = std::get_if<LoopControl>(&node.synthetic);
              wrapper && enumeration_->wrappers[wrapper->loop.value]) {
            const auto i = *enumeration_->wrappers[wrapper->loop.value];
            Require(enumeration_statements_[i].has_value() && loops_[wrapper->loop.value].has_value(), Error::kControl);
            const auto statement = *enumeration_statements_[i];
            const auto& syntax = std::get<ForInStmt>(tree_.statements[statement.value].statement);
            Require(child < block.child_count && sequences_.Child(block.first_child + child++) == statement &&
                enumeration_scopes_[i] == block.scope, Error::kSequence);
            if (enumeration_->loops[i].exhaustion_only) edges_->Consume(evaluation_.for_in->loops[i].done_edge, block, child);
            const auto offset = cursor->value - flow_.first_control.value;
            flow_.controls[offset] = tree_.statements[statement.value].origin; flow_.control_statements[offset] = statement;
            Require(pending < tasks_.size(), Error::kResourceLimit);
            tasks_[pending++] = {control_.loops[wrapper->loop.value].body, syntax.body, 0};
            cursor = node.next; continue;
          }
          Loop(*cursor, block, child, pending);
          cursor = node.next;
          continue;
        }
        const auto& source = graph_.blocks[SourceBlock(node).value];
        // Branch operations live at Before, but their successors live at Commit.
        // Only a checked empty boundary permits the predicate at Commit; no
        // normal-copy writes or effects may intervene in that relocation.
        std::optional<analysis::OpId> predicate;
        if (exceptions_) {
          const auto& phase = graph_.phases[SourceBlock(node).value];
          if (phase.phase == analysis::ExceptionPhase::kCommit && phase.operation &&
              std::holds_alternative<analysis::ValueBranch>(graph_.ops[phase.operation->value].operation))
            predicate = phase.operation;
        }
        bool switched=false;std::optional<ControlId> switch_follow;
        const auto count = predicate ? 1U : source.op_count;
        for (std::uint32_t i = 0; i < count; ++i) {
          budget_.Step();
          const analysis::OpId id = predicate.value_or(analysis::OpId{source.first_op.value + i});
          LocationScope witness(*this, At(id)); location_.statement = region.block;
          const auto& op = graph_.ops[id.value];
          if(iteration_ && evaluation_.operations[id.value].kind==EvaluationKind::kForOfEvent) {
            Iteration(id,block,child,predicate.has_value());continue;
          }
          if (enumeration_ && evaluation_.operations[id.value].kind == EvaluationKind::kForInEvent) {
            Enumeration(id, block, child, predicate.has_value()); continue;
          }
          if (exceptions_ && !predicate && std::holds_alternative<analysis::ValueBranch>(op.operation)) {
            Require((evaluation_.operations[id.value].kind == EvaluationKind::kRoot ||
                evaluation_.operations[id.value].kind == EvaluationKind::kLogicalValue) && source.edge_count == 1 && !edges_->HasCopies(source.first_edge), Error::kControl);
            continue;
          }
          if (exceptions_ && op.life == analysis::ValueLife::kLive)
            edges_->Stage(id, block, child);
          if (op.life != analysis::ValueLife::kLive ||
              (LiteralFieldPrefix(evaluation_, id) && !std::holds_alternative<analysis::ValueBranch>(op.operation)) ||
              (!original_prefixes_.empty() && original_prefixes_[id.value]) ||
              (id.value < evaluation_.phase_prefixes.size() && evaluation_.phase_prefixes[id.value]) ||
              evaluation_.operations[id.value].kind == EvaluationKind::kInline ||
              evaluation_.operations[id.value].kind == EvaluationKind::kPredicateSnapshot ||
              evaluation_.operations[id.value].kind == EvaluationKind::kPhiInput ||
              evaluation_.operations[id.value].kind == EvaluationKind::kLogicalLeft ||
              evaluation_.operations[id.value].kind == EvaluationKind::kNestedLiteral ||
              evaluation_.operations[id.value].kind == EvaluationKind::kInitializer ||
              evaluation_.operations[id.value].kind == EvaluationKind::kClassProjection ||
              evaluation_.operations[id.value].kind == EvaluationKind::kModuleGuard || GuardedInput(evaluation_, id) ||
              (evaluation_.operations[id.value].kind == EvaluationKind::kGuardedCellRead &&
               evaluation_.operations[GuardedPeer(evaluation_, id)->value].kind == EvaluationKind::kInline)) continue;
          const auto& placement = tree_.operations[*placements_[id.value]].node;
          if (evaluation_.operations[id.value].kind == EvaluationKind::kConcurrentNotification || IsReceiverGuard(tree_, id) || evaluation_.operations[id.value].kind == EvaluationKind::kCellGuard || IsAbsentHeritageTransport(graph_, evaluation_, op) ||
              std::holds_alternative<analysis::ValueJump>(op.operation) || IsEnvironmentState(op.operation) ||
              evaluation_.operations[id.value].kind == EvaluationKind::kElidedPure ||
              evaluation_.operations[id.value].kind == EvaluationKind::kAlias || evaluation_.operations[id.value].kind == EvaluationKind::kArrayCursor) {
            const auto* anchor = std::get_if<AnchorId>(&placement);
            Require(anchor != nullptr, Error::kPlacement);
            const auto* boundary = std::get_if<BlockBoundary>(&sequences_.Anchor(*anchor));
            Require(boundary && boundary->block == region.block && boundary->index == child, Error::kSequence);
            if (IsEnvironmentState(op.operation)) CellDeclarations(id, block, child, region.block);
            continue;
          }
          if (evaluation_.operations[id.value].kind == EvaluationKind::kLogicalValue) {
            Require(!logical_branches_.empty() && logical_branches_[id.value], Error::kPlacement);
            LogicalValue(logicals_[*logical_branches_[id.value]], region.block, child);
            continue;
          }
          if (LiteralFieldPrefix(evaluation_,id) && std::holds_alternative<analysis::ValueBranch>(op.operation)) {
            Require(!logical_branches_.empty()&&logical_branches_[id.value]&&flow_.expressions[id.value],Error::kPlacement);
            LogicalAssignment(logicals_[*logical_branches_[id.value]],*flow_.expressions[id.value],region.block,child);continue;
          }
          if(control_.switches && cursor->value<switch_entries_.size() && switch_entries_[cursor->value] && std::holds_alternative<analysis::ValueBranch>(op.operation)) {
            const auto r=*switch_entries_[cursor->value];Switch(r,region.block,child,pending);
            switched=true;const auto follow=control_.switches->regions[r].follow;
            switch_follow=follow?std::optional{ControlId{follow->value}}:std::nullopt;continue;
          }
          Require(child < block.child_count, Error::kSequence);
          const auto statement = sequences_.Child(block.first_child + child++);
          flow_.statements[id.value] = statement;
          const auto& syntax = tree_.statements[statement.value].statement;
          if (std::holds_alternative<analysis::ValueBranch>(op.operation)) {
            Require(std::get_if<StmtId>(&placement) && std::get<StmtId>(placement) == statement, Error::kPlacement);
            if (!logical_branches_.empty() && logical_branches_[id.value] &&
                std::holds_alternative<ExpressionStmt>(syntax)) {
              LogicalBranch(logicals_[*logical_branches_[id.value]], statement, region.block, child - 1);
              continue;
            }
            if (node.decision && std::holds_alternative<BlockStmt>(syntax)) {
              StatementDecision(node, statement, pending); continue;
            }
            const auto* branch = std::get_if<IfStmt>(&syntax);
            Require(branch && node.arms, Error::kControl);
            if (node.decision) {
              Decision(node, *branch, region.block, child - 1, pending); continue;
            }
            flow_.expressions[id.value] = branch->condition;
            const TreeOriginId origin{op.origin.value};
            if (!node.arms->taken && node.arms->fallthrough && !branch->alternate) {
              // Derive polarity from actual arm ownership, not a builder flag.
              // The omitted original taken edge must have no phi assignments.
              std::uint32_t empty = 0;
              edges_->Consume(source.first_edge, BlockStmt{}, empty);
              flow_.branch_polarity[id.value] = BranchPolarity::kFallthrough;
              Arm(node.arms->fallthrough, branch->consequent, origin,
                  analysis::EdgeId{source.first_edge.value + 1}, pending);
              continue;
            }
            Arm(node.arms->taken, branch->consequent, origin, source.first_edge, pending);
            const analysis::EdgeId fallthrough{source.first_edge.value + 1};
            if (node.arms->fallthrough || branch->alternate) {
              Require(branch->alternate.has_value(), Error::kControl);
              Arm(node.arms->fallthrough, *branch->alternate, origin, fallthrough, pending);
            } else {
              // An omitted alternate is only legal if this edge has no copy
              // statements. Consume against an empty sequence to prove that.
              std::uint32_t empty = 0;
              edges_->Consume(fallthrough, BlockStmt{}, empty);
            }
          } else if (std::holds_alternative<analysis::ValueDebugger>(op.operation)) {
            Require(std::get_if<StmtId>(&placement) && std::get<StmtId>(placement) == statement, Error::kPlacement);
            Require(std::holds_alternative<DebuggerStmt>(syntax), Error::kPlacement);
          } else if (std::holds_alternative<analysis::ValueReturn>(op.operation)) {
            Require(std::get_if<StmtId>(&placement) && std::get<StmtId>(placement) == statement, Error::kPlacement);
            const auto* returned = std::get_if<ReturnStmt>(&syntax);
            Require(returned && returned->value, Error::kPlacement);
            flow_.expressions[id.value] = *returned->value;
          } else if (const auto deferred = DeferredStore(id); deferred && std::holds_alternative<StmtId>(placement)) {
            Require(std::get<StmtId>(placement) == statement, Error::kPlacement);
            DeferredDeclaration(id, *deferred, statement, block, region.block);
          } else if (std::holds_alternative<analysis::ValueModuleWrite>(op.operation) &&
                     std::holds_alternative<StmtId>(placement)) {
            Require(std::get<StmtId>(placement) == statement && region.block == tree_.body, Error::kPlacement);
            const auto* declaration = std::get_if<DeclarationStmt>(&syntax);
            Require(declaration && declaration->initializer && !declared_[declaration->symbol.value] &&
                tree_.symbols[declaration->symbol.value].kind == BindingKind::kLocal, Error::kPlacement);
            declared_[declaration->symbol.value] = 1;
            flow_.expressions[id.value] = *declaration->initializer;
          } else if (std::holds_alternative<analysis::ValueThrow>(op.operation)) {
            Require(std::get_if<StmtId>(&placement) && std::get<StmtId>(placement) == statement, Error::kPlacement);
            const auto* thrown = std::get_if<ThrowStmt>(&syntax);
            Require(thrown != nullptr, Error::kPlacement);
            flow_.expressions[id.value] = thrown->value;
          } else {
            const auto* expression = std::get_if<ExpressionStmt>(&syntax);
            Require(expression != nullptr, Error::kSequence);
            const auto owner = evaluation_.operations[id.value].kind == EvaluationKind::kGuardedCellRead
                ? *GuardedPeer(evaluation_, id) : id;
            RootExpression(owner, expression->expression);
          }
        }
        if(switched){cursor=switch_follow;continue;}
        if (!node.arms) {
          std::uint8_t normal = 0;
          for (std::uint8_t e = 0; e < source.edge_count; ++e) {
            budget_.Step(); const analysis::EdgeId edge{source.first_edge.value + e};
            if (exceptions_ && graph_.edges[edge.value].kind == analysis::FlowEdgeKind::kException) continue;
            Require(++normal == 1, Error::kControl);
            edges_->Consume(edge, block, child);
          }
        }
        if(iteration_ && predicate) {
          const auto member=evaluation_.for_of->operations[predicate->value];
          if(member && member->role==ForOfRole::kDecision){cursor=iteration_->loops[member->protocol.value].body;continue;}
        }
        if (enumeration_ && predicate) {
          const auto member = evaluation_.for_in->operations[predicate->value];
          if (member && member->role == ForInRole::kDecision) { cursor = enumeration_->loops[member->loop].body; continue; }
        }
        cursor = enumeration_ && enumeration_->continuations[cursor->value] ? enumeration_->continuations[cursor->value]->next : node.next;
      }
      Require(child == block.child_count, Error::kSequence);
    }
    if (enumeration_) edges_->Forwarded(*enumeration_);
    if(iteration_)edges_->ProtocolOwned(*iteration_);
    edges_->Finish();
    Arrays();
    Objects();
    ExpressionScopes();
    NumericUpdates();
    DecisionBindings();
    for (std::size_t i = 0; i < tree_.symbols.size(); ++i) {
      budget_.Step();
      const auto& symbol = tree_.symbols[i];
      if (symbol.kind == BindingKind::kParameter || symbol.kind == BindingKind::kFunctionSelf) {
        Require(!declared_[i] && !assigned_[i], Error::kSnapshot);
      } else if (symbol.kind == BindingKind::kLocal ||
                 symbol.kind == BindingKind::kCapture ||
                 symbol.kind == BindingKind::kClassInner ||
                 symbol.kind == BindingKind::kModule ||
                 symbol.kind == BindingKind::kCatchParameter) {
        Require(declared_[i] && !assigned_[i], Error::kSnapshot);
      } else
        Require(declared_[i] && assigned_[i], Error::kSnapshot);
    }
    for (const auto cell : flow_.cells) { budget_.Step(); Require(cell.has_value(), Error::kPlacement); }
    if (exceptions_) {
      for (std::size_t i = 0; i < graph_.values.size(); ++i) {
        budget_.Step();
        const auto& value = graph_.values[i];
        Require(flow_.catch_parameters[i].has_value() ==
            (value.life == analysis::ValueLife::kLive &&
             std::holds_alternative<analysis::CatchDefinition>(value.origin) &&
             (!enumeration_ || !enumeration_->forwarded_blocks[std::get<analysis::CatchDefinition>(value.origin).block.value]) &&
             (!iteration_ || (!iteration_->close_blocks[std::get<analysis::CatchDefinition>(value.origin).block.value] && (iteration_->forwarding.blocks.empty() || !iteration_->forwarding.blocks[std::get<analysis::CatchDefinition>(value.origin).block.value])))), Error::kPlacement);
      }
      for (std::size_t i = 0; i < flow_.tries.size(); ++i) {
        budget_.Step(); Require(flow_.tries[i].has_value() == (!exceptions_->regions.layout.regions[i].inactive &&
            (!enumeration_ || !enumeration_->forwarded_regions[i]) &&
            (!iteration_ || (exceptions_->control.region_roles[i]!=ExceptionRegionRole::kProtocolOwned && exceptions_->control.region_roles[i]!=ExceptionRegionRole::kForwardedTry))), Error::kControl);
      }
    }
    VerifyConcurrentTree(graph_,evaluation_,tree_,flow_,budget_);
    sequences_.Finish(flow_, budget_);
    flow_.value_revision = graph_.revision;
    flow_.cfg_revision = graph_.cfg_revision;
    flow_.allocated_bytes = budget_.Bytes();
    flow_.work = budget_.Work();
    if (enumeration_) {
      flow_.forwarded_operations = std::move(enumeration_->forwarded_operations);
      flow_.forwarded_blocks = std::move(enumeration_->forwarded_blocks);
    }
    if(iteration_)flow_.for_of->view=std::move(*iteration_);
    return std::move(flow_);
  }

 private:
  void Switch(std::uint32_t index, StmtId parent, std::uint32_t& child,
              std::size_t& pending) {
    const auto& proof = *control_.switches;
    const auto& region = proof.regions[index];
    const auto& outer = sequences_.Block(parent);
    Require(child < outer.child_count && !switch_statements_[index],
            Error::kSequence);
    const auto statement = sequences_.Child(outer.first_child + child++);
    const auto* syntax =
        std::get_if<SwitchStmt>(&tree_.statements[statement.value].statement);
    Require(syntax, Error::kPlacement);
    const auto& clauses = sequences_.Block(syntax->body);
    Require(clauses.child_count == region.clause_count, Error::kSequence);
    switch_statements_[index] = statement;
    for (std::uint32_t c = 0; c < region.clause_count; ++c) {
      budget_.Step();
      const auto& expected = proof.clauses[region.first_clause + c];
      const auto id = sequences_.Child(clauses.first_child + c);
      const auto* clause =
          std::get_if<SwitchCaseStmt>(&tree_.statements[id.value].statement);
      Require(
          clause && clause->condition.has_value() == expected.test.has_value(),
          Error::kPlacement);
      const auto& body = sequences_.Block(clause->body);
      Require(tree_.scopes[body.scope.value].parent == clauses.scope,
              Error::kPlacement);
      if (expected.test) {
        const auto& t = *expected.test;
        Require(t.discriminant_on_left, Error::kControl);
        const auto* placed = std::get_if<ExprId>(
            &tree_.operations[*placements_[t.branch.value]].node);
        Require(placed && *placed == *clause->condition, Error::kPlacement);
        flow_.expressions[t.branch.value] = *clause->condition;
        flow_.branch_polarity[t.branch.value] =
            t.match == graph_.blocks[t.block.value].first_edge
                ? BranchPolarity::kTaken
                : BranchPolarity::kFallthrough;
        // Both original arcs are owned here; their phi programs must be empty.
        std::uint32_t empty = 0;
        edges_->Consume(t.match, BlockStmt{}, empty);
        edges_->Consume(t.miss, BlockStmt{}, empty);
        const auto& cmp = SwitchComparison(tree_, *clause);
        (void)cmp;
      }
      if (expected.body) {
        Require(pending < tasks_.size(), Error::kResourceLimit);
        tasks_[pending++] = {ControlId{expected.body->value}, clause->body, 0};
      } else
        Require(body.child_count == 0, Error::kSequence);
    }
    auto at = region.entry;
    while (true) {
      budget_.Step();
      const auto& block = graph_.blocks[at.value];
      const auto test = switch_tests_[at.value];
      if (at != region.entry)
        for (std::uint32_t o = 0; o < block.op_count; ++o) {
          const analysis::OpId op{block.first_op.value + o};
          if (test && proof.clauses[*test].test->branch == op) continue;
          DecisionOperation(op, parent, child - 1);
        }
      if (!test) {
        std::uint32_t empty = 0;
        edges_->Consume(block.first_edge, BlockStmt{}, empty);
      }
      const auto edge =
          test ? proof.clauses[*test].test->miss : block.first_edge;
      const auto next = graph_.edges[edge.value].target;
      if (!proof.blocks[next.value] ||
          proof.blocks[next.value]->region != index ||
          proof.blocks[next.value]->clause)
        break;
      at = next;
    }
  }
  analysis::WorkBlockId SourceBlock(const ControlNode& node) const {
    if(!exceptions_ || !evaluation_.for_of)return node.block;
    const auto index=static_cast<std::size_t>(&node-control_.nodes.data());
    Require(index<exceptions_->control.units.size(),Error::kControl);
    const auto* phase=std::get_if<PhaseControlUnit>(&exceptions_->control.units[index]);
    Require(phase!=nullptr,Error::kControl);return phase->block;
  }
  const analysis::WorkBlock& DecisionBlock(const ControlNode& node) const {
    Require(std::holds_alternative<std::monostate>(node.synthetic), Error::kControl);
    if (exceptions_) {
      const auto index = static_cast<std::size_t>(&node - control_.nodes.data());
      Require(index < exceptions_->control.units.size(), Error::kControl);
      const auto* phase = std::get_if<PhaseControlUnit>(&exceptions_->control.units[index]);
      Require(phase && phase->block == SourceBlock(node), Error::kControl);
    }
    Require(SourceBlock(node).value < graph_.blocks.size(), Error::kControl);
    return graph_.blocks[SourceBlock(node).value];
  }
  const SelectionRegion* DecisionSelection(const ControlNode& node) const {
    if (!node.arms || !node.next) return nullptr;
    const auto branch = DecisionPredicate(node);
    if (logical_branches_.empty() || !logical_branches_[branch.value] ||
        (evaluation_.operations[branch.value].kind != EvaluationKind::kLogicalValue &&
         evaluation_.operations[branch.value].kind != EvaluationKind::kRoot)) return nullptr;
    const auto& selection = logicals_[*logical_branches_[branch.value]];
    if (selection.parent || selection.prefix_parent ||
        SourceBlock(control_.nodes[node.next->value]) != selection.join) return nullptr;
    return &selection;
  }
  analysis::OpId DecisionPredicate(const ControlNode& node) const {
    const auto& block = DecisionBlock(node);
    if (exceptions_) {
      const auto& phase = graph_.phases[SourceBlock(node).value];
      Require(phase.phase == analysis::ExceptionPhase::kCommit && phase.operation &&
          std::holds_alternative<analysis::ValueBranch>(graph_.ops[phase.operation->value].operation), Error::kControl);
      return *phase.operation;
    }
    Require(block.op_count != 0, Error::kControl);
    const analysis::OpId op{block.first_op.value + block.op_count - 1};
    Require(std::holds_alternative<analysis::ValueBranch>(graph_.ops[op.value].operation), Error::kControl);
    return op;
  }
  void DecisionOperation(analysis::OpId op, StmtId parent, std::uint32_t child,
      ExprId* condition = nullptr, std::optional<analysis::OpId> root = {},
      std::optional<analysis::OpId> predicate = {}) {
    LocationScope witness(*this, At(op));
    budget_.Step(); const auto& original = graph_.ops[op.value];
    if (original.life != analysis::ValueLife::kLive) return;
    if (exceptions_) {
      Require(root.has_value(), Error::kControl);
      const auto site = graph_.phases[original.block.value].instruction.value;
      const auto owner = graph_.phases[graph_.ops[root->value].block.value].instruction.value;
      const auto& scope = exceptions_->regions.layout.scopes[site];
      const auto& enclosing = exceptions_->regions.layout.scopes[owner];
      Require(scope.has_value() == enclosing.has_value() &&
          (!scope || (scope->region == enclosing->region && scope->part == enclosing->part)), Error::kPlacement);
      std::uint32_t empty = 0;
      edges_->Stage(op, BlockStmt{}, empty);
    }
    const auto kind = evaluation_.operations[op.value].kind;
    const auto& placement = tree_.operations[*placements_[op.value]].node;
    if (const auto* anchor = std::get_if<AnchorId>(&placement)) {
      // IndexPlacements independently proves the semantic reason for each
      // anchor (including actual initialized-cell facts). Only its boundary
      // belongs to this control consumer; no second producer-list admission.
      const auto* boundary = std::get_if<BlockBoundary>(&sequences_.Anchor(*anchor));
      Require(boundary && boundary->block == parent && boundary->index == child, Error::kSequence);
      Require(!IsEnvironmentState(original.operation), Error::kPlacement);
      return;
    }
    if (const auto* property = std::get_if<PropertyId>(&placement)) {
      // A literal commit is owned by its allocation expression, not a second
      // expression at the decision boundary. IndexPlacements established this
      // unique PropertyId; Objects and OperandCheck independently recheck its
      // allocation, order, key/value and exception correspondence. Keep all
      // other property/root dispositions refused (decision_literal_test).
      Require(kind == EvaluationKind::kInitializer &&
          flow_.initializers[op.value] == *property &&
          tree_.properties[property->value].origin.value == original.origin.value,
          Error::kPlacement);
      return;
    }
    // Reconstruct the load/guard boundary independently: the checked native
    // read executes once at the original guard, not at the raw load or again
    // when the enclosing shared decision is printed.
    if (GuardedInput(evaluation_, op)) return;
    const auto source = kind == EvaluationKind::kGuardedCellRead ? *GuardedPeer(evaluation_, op) : op;
    const auto source_kind = evaluation_.operations[source.value].kind;
    if (kind == EvaluationKind::kGuardedCellRead)
      Require(flow_.expressions[source.value] == flow_.expressions[op.value], Error::kPlacement);
    // Actual expression ownership/operands are checked by the common consumers;
    // a shared decision does not give the operation another executable root.
    if (source_kind != EvaluationKind::kRoot && std::holds_alternative<ExprId>(placement)) return;
    if (source_kind == EvaluationKind::kRoot && !std::holds_alternative<analysis::ValueJump>(original.operation)) {
      const auto shape = DescribeEvaluation(graph_.ops[source.value].operation);
      Require(exceptions_ && condition && shape && shape->expression &&
          !std::holds_alternative<analysis::ValueBranch>(original.operation) &&
          !std::holds_alternative<analysis::ValueReturn>(original.operation) &&
          !std::holds_alternative<analysis::ValueThrow>(original.operation), Error::kPlacement);
      const auto* sequence = std::get_if<SequenceExpr>(&tree_.expressions[condition->value].expression);
      Require(sequence && predicate && tree_.expressions[condition->value].origin.value ==
          graph_.ops[predicate->value].origin.value, Error::kSequence);
      RootExpression(source, sequence->left); *condition = sequence->right;
      return;
    }
    throw Error::kPlacement;
  }
  void DecisionRead(ExprId expression, SymbolId saved) {
    const auto* read = std::get_if<SymbolExpr>(&tree_.expressions[expression.value].expression);
    Require(read && read->symbol == saved, Error::kSnapshot);
    decision_expressions_[expression.value] = 1;
  }
  void DecisionBindings() {
    if (decision_expressions_.empty()) return;
    // Every access to this private staging cell belongs to a checked decision.
    // A name/reason alone cannot authorize an extra read, write, or capture.
    for (std::size_t i = 0; i < tree_.expressions.size(); ++i) {
      budget_.Step(); const auto& expression = tree_.expressions[i].expression;
      std::optional<SymbolId> symbol;
      if (const auto* read = std::get_if<SymbolExpr>(&expression)) symbol = read->symbol;
      if (const auto* assignment = std::get_if<AssignmentExpr>(&expression))
        if (const auto* target = std::get_if<SymbolTarget>(&assignment->target)) symbol = target->symbol;
      if (const auto* update = std::get_if<UpdateExpr>(&expression)) symbol = update->symbol;
      if (symbol && tree_.symbols[symbol->value].temporary == TemporaryReason::kDecisionPredicate)
        Require(decision_expressions_[i], Error::kSnapshot);
    }
    for (const auto symbol : tree_.closure_captures) {
      budget_.Step(); Require(tree_.symbols[symbol.value].temporary != TemporaryReason::kDecisionPredicate, Error::kSnapshot);
    }
  }
  void DecisionStatements(const ControlNode& node, StmtId parent, std::uint32_t& child,
      bool predicate) {
    const auto& source = DecisionBlock(node);
    const auto& block = sequences_.Block(parent);
    for (std::uint32_t i = 0; i < source.op_count; ++i) {
      budget_.Step(); const analysis::OpId id{source.first_op.value + i};
      const auto& op = graph_.ops[id.value];
      if (op.life != analysis::ValueLife::kLive) continue;
      if (predicate && id == DecisionPredicate(node)) continue;
      const auto kind = evaluation_.operations[id.value].kind;
      if (kind == EvaluationKind::kLogicalValue) {
        const auto* selection = DecisionSelection(node);
        Require(selection && selection->branch == id, Error::kControl);
        LogicalValue(*selection, parent, child); continue;
      }
      if (kind == EvaluationKind::kInline || kind == EvaluationKind::kPredicateSnapshot ||
          kind == EvaluationKind::kPhiInput || kind == EvaluationKind::kLogicalLeft ||
          kind == EvaluationKind::kNestedLiteral || kind == EvaluationKind::kInitializer ||
          kind == EvaluationKind::kModuleGuard) continue;
      const auto& placement = tree_.operations[*placements_[id.value]].node;
      if (kind == EvaluationKind::kConcurrentNotification || IsReceiverGuard(tree_, id) || kind == EvaluationKind::kCellGuard ||
          kind == EvaluationKind::kClassProjection || IsAbsentHeritageTransport(graph_, evaluation_, op) ||
          std::holds_alternative<analysis::ValueJump>(op.operation) || IsEnvironmentState(op.operation) ||
          kind == EvaluationKind::kElidedPure || kind == EvaluationKind::kAlias || kind == EvaluationKind::kArrayCursor) {
        const auto* anchor = std::get_if<AnchorId>(&placement);
        Require(anchor, Error::kPlacement);
        const auto* boundary = std::get_if<BlockBoundary>(&sequences_.Anchor(*anchor));
        Require(boundary && boundary->block == parent && boundary->index == child, Error::kSequence);
        if (IsEnvironmentState(op.operation)) CellDeclarations(id, block, child, parent);
        continue;
      }
      Require(child < block.child_count, Error::kSequence);
      const auto statement = sequences_.Child(block.first_child + child++);
      flow_.statements[id.value] = statement;
      const auto& syntax = tree_.statements[statement.value].statement;
      if (std::holds_alternative<analysis::ValueDebugger>(op.operation)) {
        Require(std::get_if<StmtId>(&placement) && std::get<StmtId>(placement) == statement &&
            std::holds_alternative<DebuggerStmt>(syntax), Error::kPlacement);
      } else {
        const auto* expression = std::get_if<ExpressionStmt>(&syntax);
        Require(expression, Error::kSequence);
        if (const auto* selection = DecisionSelection(node); selection && selection->branch == id) {
          Require(kind == EvaluationKind::kRoot, Error::kPlacement);
          LogicalAssignment(*selection, expression->expression, parent, child - 1);
        } else RootExpression(id, expression->expression);
      }
    }
    if (!predicate && !DecisionSelection(node)) for (std::uint8_t e = 0; e < source.edge_count; ++e) {
      budget_.Step(); edges_->Consume(analysis::EdgeId{source.first_edge.value + e}, block, child);
    }
  }
  void StatementDecision(const ControlNode& root, StmtId wrapper, std::size_t& pending) {
    Require(!exceptions_ && root.decision && root.arms, Error::kControl);
    if (decision_expressions_.empty()) budget_.Array(decision_expressions_, tree_.expressions.size());
    const auto& top = sequences_.Block(wrapper);
    Require(top.child_count != 0, Error::kSequence);
    const auto declaration_id = sequences_.Child(top.first_child);
    const auto* declaration = std::get_if<DeclarationStmt>(&tree_.statements[declaration_id.value].statement);
    Require(declaration && !declaration->initializer, Error::kSnapshot);
    const auto saved = declaration->symbol;
    const auto& symbol = tree_.symbols[saved.value];
    Require(symbol.kind == BindingKind::kCompilerTemporary && symbol.scope == top.scope &&
        symbol.declaration == DeclarationKind::kLet && symbol.temporary == TemporaryReason::kDecisionPredicate &&
        !declared_[saved.value] && !assigned_[saved.value], Error::kSnapshot);
    const auto* binding = std::get_if<BindingOrigin>(&tree_.origins[tree_.statements[declaration_id.value].origin.value]);
    Require(binding && binding->symbol == saved, Error::kSnapshot);
    declared_[saved.value] = assigned_[saved.value] = 1;
    const auto root_predicate = DecisionPredicate(root);
    const TreeOriginId origin{graph_.ops[root_predicate.value].origin.value};
    Require(tree_.statements[wrapper.value].origin == origin, Error::kPlacement);
    const auto* part = &root;
    auto parent = wrapper;
    std::uint32_t child = 1, top_child = 2;
    bool selected_taken = root.arms->taken.has_value();
    while (true) {
      budget_.Step(); const auto& source = DecisionBlock(*part);
      Require(!edges_->HasCopies(source.first_edge) &&
          !edges_->HasCopies(analysis::EdgeId{source.first_edge.value + 1}), Error::kControl);
      const auto branch = DecisionPredicate(*part);
      const TreeOriginId here{graph_.ops[branch.value].origin.value};
      const auto& block = sequences_.Block(parent);
      Require(child < block.child_count, Error::kSequence);
      const auto statement = sequences_.Child(block.first_child + child++);
      const auto* expression = std::get_if<ExpressionStmt>(&tree_.statements[statement.value].statement);
      Require(expression && tree_.statements[statement.value].origin == here, Error::kPlacement);
      const auto* assignment = std::get_if<AssignmentExpr>(&tree_.expressions[expression->expression.value].expression);
      Require(assignment && std::get_if<SymbolTarget>(&assignment->target) &&
          std::get<SymbolTarget>(assignment->target).symbol == saved &&
          tree_.expressions[expression->expression.value].origin == here, Error::kSnapshot);
      decision_expressions_[expression->expression.value] = 1;
      if (part != &root) Require(flow_.expressions[branch.value] == assignment->value, Error::kPlacement);
      flow_.expressions[branch.value] = assignment->value;
      flow_.branch_polarity[branch.value] = selected_taken ? BranchPolarity::kTaken : BranchPolarity::kFallthrough;
      for (std::uint8_t e = 0; e < source.edge_count; ++e) {
        budget_.Step();
        if (part == &root && e == (root.arms->taken ? 0U : 1U)) continue;
        if (!part->decision && (selected_taken ? part->arms->fallthrough : part->arms->taken) &&
            e == (selected_taken ? 1U : 0U)) continue;
        std::uint32_t empty = 0;
        edges_->Consume(analysis::EdgeId{source.first_edge.value + e}, BlockStmt{}, empty);
      }
      if (part != &root) Require(child == block.child_count, Error::kSequence);
      if (!part->decision) break;
      Require(top_child < top.child_count, Error::kSequence);
      const auto guard_id = sequences_.Child(top.first_child + top_child++);
      const auto* guard = std::get_if<IfStmt>(&tree_.statements[guard_id.value].statement);
      Require(guard && !guard->alternate && tree_.statements[guard_id.value].origin == here, Error::kSequence);
      const auto* negated = std::get_if<UnaryExpr>(&tree_.expressions[guard->condition.value].expression);
      Require(negated && negated->operation == TreeUnary::kNot, Error::kControl);
      DecisionRead(negated->operand, saved);
      parent = guard->consequent; child = 0;
      auto next = part->decision->evaluate;
      const auto predicate = part->decision->predicate.value_or(next);
      while (next != predicate) {
        budget_.Step(); const auto& linear = control_.nodes[next.value];
        Require(linear.next && ((!linear.arms && !linear.decision) || DecisionSelection(linear)), Error::kControl);
        DecisionStatements(linear, parent, child, false);
        next = *linear.next;
      }
      selected_taken = part->decision->shared_on_taken;
      part = &control_.nodes[predicate.value];
      DecisionStatements(*part, parent, child, true);
    }
    Require(top_child + 1 == top.child_count, Error::kSequence);
    const auto final_id = sequences_.Child(top.first_child + top_child);
    const auto* final = std::get_if<IfStmt>(&tree_.statements[final_id.value].statement);
    Require(final && tree_.statements[final_id.value].origin == origin, Error::kControl);
    DecisionRead(final->condition, saved);
    const auto& first = DecisionBlock(root);
    Arm(root.arms->taken ? root.arms->taken : root.arms->fallthrough, final->consequent, origin,
        analysis::EdgeId{first.first_edge.value + (root.arms->taken ? 0U : 1U)}, pending);
    const auto fallback = selected_taken ? part->arms->fallthrough : part->arms->taken;
    Require(fallback.has_value() == final->alternate.has_value(), Error::kControl);
    if (fallback) {
      const auto& last = DecisionBlock(*part);
      const auto predicate = DecisionPredicate(*part);
      Arm(fallback, *final->alternate, TreeOriginId{graph_.ops[predicate.value].origin.value},
          analysis::EdgeId{last.first_edge.value + (selected_taken ? 1U : 0U)}, pending);
    }
  }
  bool DecisionCopies(const analysis::WorkBlock& block) const {
    return edges_->HasCopies(block.first_edge) || edges_->HasCopies(analysis::EdgeId{block.first_edge.value + 1});
  }
  void Decision(const ControlNode& node, const IfStmt& syntax, StmtId parent,
      std::uint32_t child, std::size_t& pending) {
    const auto& root = DecisionBlock(node);
    const auto root_predicate = DecisionPredicate(node);
    const auto* part = &node;
    bool selected_taken = node.arms->taken.has_value();
    auto expression = syntax.condition;
    std::optional<ControlId> linear_start;
    while (true) {
      budget_.Step(); const auto& block = DecisionBlock(*part);
      const bool copies = DecisionCopies(block);
      const auto at = DecisionPredicate(*part).value;
      auto condition = expression;
      if (part->decision) {
        const auto* logical = std::get_if<LogicalExpr>(&tree_.expressions[expression.value].expression);
        Require(logical && logical->operation == TreeLogical::kOr, Error::kControl);
        condition = logical->left; expression = logical->right;
      }
      if (linear_start) {
        auto next = *linear_start;
        const auto terminal = ControlId{static_cast<std::uint32_t>(part - control_.nodes.data())};
        while (next != terminal) {
          budget_.Step(); const auto& linear = control_.nodes[next.value];
          const auto& path = DecisionBlock(linear);
          Require(linear.next && ((!linear.arms && !linear.decision) || DecisionSelection(linear)), Error::kControl);
          for (std::uint32_t i = 0; i < path.op_count; ++i) {
            const analysis::OpId op{path.first_op.value + i};
            if (exceptions_ && std::holds_alternative<analysis::ValueBranch>(graph_.ops[op.value].operation)) {
              Require(evaluation_.operations[op.value].kind == EvaluationKind::kRoot &&
                  path.edge_count == 1 && !edges_->HasCopies(path.first_edge), Error::kControl);
              continue;
            }
            if (evaluation_.operations[op.value].kind == EvaluationKind::kLogicalValue) {
              const auto* selection = DecisionSelection(linear);
              Require(selection && selection->branch == op, Error::kControl);
              LogicalValue(*selection, parent, child);
            } else DecisionOperation(op, parent, child, &condition, root_predicate, analysis::OpId{at});
          }
          if (exceptions_) if (const auto* selection = DecisionSelection(linear)) {
            const auto site = graph_.phases[graph_.ops[selection->branch.value].block.value].instruction.value;
            const auto owner = graph_.phases[graph_.ops[root_predicate.value].block.value].instruction.value;
            const auto& scope = exceptions_->regions.layout.scopes[site];
            const auto& enclosing = exceptions_->regions.layout.scopes[owner];
            Require(scope.has_value() == enclosing.has_value() &&
                (!scope || (scope->region == enclosing->region && scope->part == enclosing->part)), Error::kPlacement);
            if (evaluation_.operations[selection->branch.value].kind == EvaluationKind::kLogicalValue) {
              LogicalValue(*selection, parent, child);
            } else {
              const auto* sequence = std::get_if<SequenceExpr>(&tree_.expressions[condition.value].expression);
              Require(sequence && tree_.expressions[condition.value].origin.value == graph_.ops[at].origin.value,
                  Error::kSequence);
              const auto& placement = tree_.operations[*placements_[selection->branch.value]].node;
              Require(std::get_if<ExprId>(&placement) && std::get<ExprId>(placement) == sequence->left, Error::kPlacement);
              LogicalAssignment(*selection, sequence->left, parent, child);
              condition = sequence->right;
            }
          }
          for (std::uint8_t e = 0; !DecisionSelection(linear) && e < path.edge_count; ++e) {
            budget_.Step(); const analysis::EdgeId edge{path.first_edge.value + e};
            if (graph_.edges[edge.value].kind == analysis::FlowEdgeKind::kException) continue;
            std::uint32_t empty = 0;
            edges_->Consume(edge, BlockStmt{}, empty);
          }
          next = *linear.next;
        }
      }
      if (copies) {
        const auto* choice = std::get_if<ConditionalExpr>(&tree_.expressions[condition.value].expression);
        Require(choice, Error::kControl);
        edges_->Decision(analysis::EdgeId{block.first_edge.value + (selected_taken ? 0U : 1U)}, choice->consequent, true);
        edges_->Decision(analysis::EdgeId{block.first_edge.value + (selected_taken ? 1U : 0U)}, choice->alternate, false);
        condition = choice->condition;
      }
      flow_.expressions[at] = condition;
      flow_.branch_polarity[at] = selected_taken ? BranchPolarity::kTaken : BranchPolarity::kFallthrough;
      if (part != &node) {
        Require(std::get_if<ExprId>(&tree_.operations[*placements_[at]].node) &&
            std::get<ExprId>(tree_.operations[*placements_[at]].node) == condition, Error::kPlacement);
        if (exceptions_) {
          std::uint32_t empty = 0;
          edges_->Stage(analysis::OpId{at}, BlockStmt{}, empty);
        } else for (std::uint32_t i = 0; i + 1 < block.op_count; ++i)
          DecisionOperation(analysis::OpId{block.first_op.value + i}, parent, child);
      }
      for (std::uint8_t e = 0; !copies && e < block.edge_count; ++e) {
        budget_.Step();
        if (part == &node && e == (node.arms->taken ? 0U : 1U)) continue;
        // A retained final fallback owns its original transfer in the alternate.
        if (!part->decision && syntax.alternate && e == (selected_taken ? 1U : 0U)) continue;
        std::uint32_t empty = 0;
        edges_->Consume(analysis::EdgeId{block.first_edge.value + e}, BlockStmt{}, empty);
      }
      if (!part->decision) break;
      const auto& decision = *part->decision;
      linear_start = decision.evaluate;
      const auto predicate = decision.predicate.value_or(*linear_start);
      selected_taken = decision.shared_on_taken;
      part = &control_.nodes[predicate.value];
    }
    const TreeOriginId origin{graph_.ops[root_predicate.value].origin.value};
    Arm(node.arms->taken ? node.arms->taken : node.arms->fallthrough, syntax.consequent, origin,
        analysis::EdgeId{root.first_edge.value + (node.arms->taken ? 0U : 1U)}, pending, !DecisionCopies(root));
    const auto fallback = selected_taken ? part->arms->fallthrough : part->arms->taken;
    Require(fallback.has_value() == syntax.alternate.has_value(), Error::kControl);
    if (fallback) {
      const auto& last = DecisionBlock(*part);
      const auto predicate = DecisionPredicate(*part);
      Arm(fallback, *syntax.alternate, TreeOriginId{graph_.ops[predicate.value].origin.value},
          analysis::EdgeId{last.first_edge.value + (selected_taken ? 1U : 0U)}, pending, !DecisionCopies(last));
    }
  }
  void LogicalBranch(const SelectionRegion& region, StmtId statement, StmtId parent, std::uint32_t child) {
    const auto root = std::get<ExpressionStmt>(tree_.statements[statement.value].statement).expression;
    Require(tree_.statements[statement.value].origin.value == graph_.ops[region.branch.value].origin.value,
        Error::kPlacement);
    LogicalAssignment(region, root, parent, child);
  }
  void LogicalAssignment(const SelectionRegion& region, ExprId root, StmtId parent, std::uint32_t child) {
    const auto* assignment = std::get_if<AssignmentExpr>(&tree_.expressions[root.value].expression);
    Require(assignment != nullptr, Error::kPlacement);
    const auto* target = std::get_if<SymbolTarget>(&assignment->target);
    if (region.consequent) {
      Require(target && tree_.expressions[root.value].origin.value == graph_.ops[region.branch.value].origin.value, Error::kPlacement);
      Conditional(region, assignment->value, target->symbol, parent, child);
      return;
    }
    const auto* logical = std::get_if<LogicalExpr>(&tree_.expressions[assignment->value.value].expression);
    Require(target && logical && logical->operation == region.operation &&
        flow_.expressions[region.boolean.value] == assignment->value &&
        tree_.expressions[root.value].origin.value == graph_.ops[region.branch.value].origin.value, Error::kPlacement);
    const auto rhs = std::get<analysis::ValueId>(region.right);
    const auto producer = std::get<analysis::OpResult>(graph_.values[rhs.value].origin).op;
    Require(flow_.expressions[producer.value] == logical->right, Error::kPlacement);
    flow_.expressions[region.branch.value] = assignment->value;
    flow_.logical_operations[region.boolean.value] = flow_.logical_operations[region.branch.value] = region.operation;
    LogicalTruths(region,logical->left);
    LogicalEdges(region, target->symbol, logical->left, logical->right, parent, child);
    LogicalPrefixes(region, parent, child);
  }
  bool SelectionOrigin(const SelectionRegion& region, ExprId expression) const {
    const auto origin = tree_.expressions[expression.value].origin;
    const TreeOriginId branch{graph_.ops[region.branch.value].origin.value};
    if (origin == branch) return true;
    // A transparent conversion can share either conditional or logical syntax
    // with its original branch. The independent correspondence pass proves
    // every combined site's membership and unique expression ownership.
    if (std::holds_alternative<CombinedOrigin>(tree_.origins[origin.value])) return true;
    const auto input = evaluation_.operations[region.branch.value].consumer;
    if (!exceptions_ || !input) return false;
    const auto* owner = std::get_if<analysis::OpId>(&graph_.uses[input->value].owner);
    if (!owner || owner->value >= evaluation_.phase_transports.size() || !evaluation_.phase_transports[owner->value]) return false;
    return origin.value == graph_.ops[owner->value].origin.value;
  }
  void Conditional(const SelectionRegion& region, ExprId expression, std::optional<SymbolId> target,
      StmtId parent, std::uint32_t child) {
    std::size_t pending = 1;
    conditional_tasks_[0] = {static_cast<std::size_t>(&region - logicals_.data()), expression, target};
    while (pending) {
      budget_.Step(); const auto task = conditional_tasks_[--pending]; const auto& current = logicals_[task.region];
      const auto* conditional = std::get_if<ConditionalExpr>(&tree_.expressions[task.expression.value].expression);
      const auto* logical = std::get_if<LogicalExpr>(&tree_.expressions[task.expression.value].expression);
      Require((current.logical ? logical && logical->operation == current.logical->operation : conditional != nullptr) &&
          SelectionOrigin(current, task.expression), Error::kPlacement);
      auto selected_condition = current.logical ? logical->left : conditional->condition;
      const auto* call = FindPhaseSelectionOperand(graph_, evaluation_, current.branch);
      const auto* prefixes = current.original_prefix ? &*current.original_prefix : call ? &call->prefix : nullptr;
      if (prefixes) {
        for (const auto prefix : *prefixes) {
          budget_.Step(); const auto& node = tree_.expressions[selected_condition.value];
          const auto* sequence = std::get_if<SequenceExpr>(&node.expression);
          Require(sequence && node.origin.value == graph_.ops[current.branch.value].origin.value, Error::kPlacement);
          RootExpression(prefix, sequence->left);
          selected_condition = sequence->right;
        }
      }
      ExprId consequent, alternate;
      if (current.logical) {
        consequent = current.logical->keep_consequent ? selected_condition : logical->right;
        alternate = current.logical->keep_consequent ? logical->right : selected_condition;
        const TreeSelectionTruth truth{current.logical->shared_owner.value_or(current.branch), current.logical->value, selected_condition, current.logical->input};
        flow_.expressions[current.branch.value] = task.expression;
        const auto mark = [&](analysis::OpId id) {
          Require(flow_.expressions[id.value] == task.expression && !flow_.selection_truths[id.value], Error::kPlacement);
          flow_.selection_truths[id.value] = truth;
          flow_.logical_operations[id.value] = current.logical->operation;
        };
        for(const auto& test:current.tests) {
          budget_.Step();mark(test.operation);
          flow_.selection_truths[test.operation.value]->test=test.meaning;
        }
      } else {
        consequent = conditional->consequent; alternate = conditional->alternate;
      }
      if (!current.logical && current.conditions.empty()) {
        Require(flow_.expressions[current.boolean.value] == selected_condition, Error::kPlacement);
      } else if (!current.logical) {
        auto condition = selected_condition;
        for (std::size_t i = 0; i < current.conditions.size(); ++i) {
          budget_.Step(); const auto& scalar = current.conditions[i];
          auto expression = condition;
          if (i + 1 != current.conditions.size()) {
            const auto* logical = std::get_if<LogicalExpr>(&tree_.expressions[condition.value].expression);
            Require(logical && logical->operation == TreeLogical::kOr, Error::kControl);
            expression = logical->left; condition = logical->right;
          }
          flow_.branch_polarity[scalar.branch.value] = scalar.shared_on_taken ? BranchPolarity::kTaken : BranchPolarity::kFallthrough;
          if (i == 0) flow_.branch_predicates[scalar.branch.value] = expression;
          else Require(flow_.expressions[scalar.branch.value] == expression, Error::kPlacement);
        }
      }
      if (!current.conditions.empty()) {
        for (const auto id : current.condition_blocks) {
          budget_.Step(); const auto& block = graph_.blocks[control_.nodes[id.value].block.value];
          for (std::uint32_t i = 0; i < block.op_count; ++i) {
            budget_.Step(); const analysis::OpId op{block.first_op.value + i};
            if (std::holds_alternative<analysis::ValueBranch>(graph_.ops[op.value].operation) ||
                evaluation_.operations[op.value].kind == EvaluationKind::kModuleGuard) continue;
            DecisionOperation(op, parent, child, nullptr, exceptions_ ? std::optional{current.branch} : std::nullopt);
          }
          for (std::uint8_t i = 0; i < block.edge_count; ++i) {
            budget_.Step(); std::uint32_t empty = 0;
            edges_->Consume(analysis::EdgeId{block.first_edge.value + i}, BlockStmt{}, empty);
          }
        }
      }
      const auto arm = [&](const SelectionOperand& input, ExprId actual) {
        if (current.logical && current.logical->shared_right) {
          if (const auto* nested = std::get_if<SelectionChild>(&input)) {
            Require(logical_branches_[nested->branch.value].has_value(), Error::kPlacement);
            const auto index = *logical_branches_[nested->branch.value];
            const auto& part = logicals_[index];
            Require(part.parent == current.branch && part.merged == current.merged &&
                part.logical && part.logical->shared_owner == current.logical->shared_owner.value_or(current.branch) &&
                flow_.expressions[nested->branch.value] == task.expression && pending < conditional_tasks_.size(), Error::kPlacement);
            conditional_tasks_[pending++] = {index, task.expression, task.target};
            return;
          }
          const auto* snapshot = std::get_if<SelectionSnapshot>(&input);
          actual = snapshot && ResolveEvaluationValue(evaluation_, snapshot->value) == current.logical->value
              ? selected_condition : logical->right;
        }
        if (const auto* snapshot = std::get_if<SelectionSnapshot>(&input)) {
          if (current.logical && actual == selected_condition &&
              ResolveEvaluationValue(evaluation_, snapshot->value) == current.logical->value) {
            if (!evaluation_.joint_selection_uses.empty() && !evaluation_.logical_values.empty() &&
                evaluation_.logical_values[current.logical->value.value]) {
              const auto found=std::lower_bound(evaluation_.joint_selection_uses.begin(),evaluation_.joint_selection_uses.end(),current.logical->value,
                  [](const auto& proof,auto value){return proof.value<value;});
              if(found!=evaluation_.joint_selection_uses.end() && found->value==current.logical->value) {
                const auto inner=*evaluation_.logical_values[found->value.value];
                Require(flow_.expressions[inner.value]==actual && current.logical->input==evaluation_.operations[inner.value].consumer,
                    Error::kPlacement);
                return; // The original keep edge retains this evaluated subtree.
              }
            }
            const auto* result = std::get_if<analysis::OpResult>(&graph_.values[current.logical->value.value].origin);
            if (result && evaluation_.operations[result->op.value].kind == EvaluationKind::kPredicateSnapshot &&
                evaluation_.operations[result->op.value].consumer == current.logical->input) {
              if (flow_.receivers[result->op.value] != ReceiverValue::kNone) {
                Require(flow_.expressions[result->op.value] == actual, Error::kSnapshot);
                return;
              }
              const auto* save = std::get_if<AssignmentExpr>(&tree_.expressions[actual.value].expression);
              const auto* target = save ? std::get_if<SymbolTarget>(&save->target) : nullptr;
              Require(save && target && flow_.snapshots[result->op.value] == target->symbol &&
                  flow_.expressions[result->op.value] == save->value, Error::kSnapshot);
              return;
            }
          }
          Require(actual.value < tree_.expressions.size() &&
              (std::holds_alternative<SymbolExpr>(tree_.expressions[actual.value].expression) ||
               std::holds_alternative<ThisExpr>(tree_.expressions[actual.value].expression)), Error::kPlacement);
          // LogicalEdges retains this actual occurrence on its original phi
          // edge. The independent operand checker verifies the exact snapshot.
          return;
        }
        if (const auto* nested = std::get_if<SelectionChild>(&input)) {
          Require(nested->branch.value < logical_branches_.size() && logical_branches_[nested->branch.value], Error::kPlacement);
          const auto index = *logical_branches_[nested->branch.value];
          Require(logicals_[index].parent == current.branch &&
              (logicals_[index].merged == current.merged || logicals_[index].forwarded) &&
              flow_.expressions[nested->branch.value] == actual && pending < conditional_tasks_.size(), Error::kPlacement);
          conditional_tasks_[pending++] = {index, actual, logicals_[index].forwarded ? std::nullopt : task.target};
        } else {
          const auto producer = std::get<analysis::OpResult>(graph_.values[std::get<analysis::ValueId>(input).value].origin).op;
          Require(flow_.expressions[producer.value] == actual, Error::kPlacement);
        }
      };
      for (const auto prefix : current.prefix_regions) {
        budget_.Step(); const auto branch = DecisionPredicate(control_.nodes[prefix.value]);
        Require(logical_branches_[branch.value].has_value(), Error::kPlacement);
        const auto index = *logical_branches_[branch.value];
        Require(logicals_[index].prefix_parent == current.branch && flow_.expressions[branch.value] &&
            evaluation_.operations[branch.value].kind == EvaluationKind::kLogicalValue &&
            pending < conditional_tasks_.size(), Error::kPlacement);
        conditional_tasks_[pending++] = {index, *flow_.expressions[branch.value], {}};
      }
      for (const auto branch : current.arm_prefixes) {
        budget_.Step(); Require(logical_branches_[branch.value].has_value(), Error::kPlacement);
        const auto index = *logical_branches_[branch.value];
        Require(logicals_[index].prefix_parent == current.branch && flow_.expressions[branch.value] &&
            evaluation_.operations[branch.value].kind == EvaluationKind::kLogicalValue &&
            pending < conditional_tasks_.size(), Error::kPlacement);
        conditional_tasks_[pending++] = {index, *flow_.expressions[branch.value], {}};
      }
      Require(current.consequent.has_value(), Error::kPlacement);
      arm(*current.consequent, consequent); arm(current.right, alternate);
      flow_.expressions[current.branch.value] = task.expression;
      if (!current.logical) flow_.conditional_operations[current.branch.value] = 1;
      if (current.logical && current.logical->shared_right) {
        const auto leaf = [&](const SelectionOperand& input) {
          const auto* snapshot = std::get_if<SelectionSnapshot>(&input);
          return snapshot && ResolveEvaluationValue(evaluation_, snapshot->value) == current.logical->value
              ? selected_condition : logical->right;
        };
        consequent = leaf(*current.consequent); alternate = leaf(current.right);
      }
      LogicalEdges(current, task.target, consequent, alternate, parent, child);
    }
  }

  void LogicalValue(const SelectionRegion& region, StmtId parent, std::uint32_t child) {
    const auto root = flow_.expressions[region.branch.value];
    if (region.consequent) {
      Require(root.has_value(), Error::kPlacement);
      Conditional(region, *root, {}, parent, child); return;
    }
    Require(root && flow_.expressions[region.boolean.value] == root, Error::kPlacement);
    const auto* logical = std::get_if<LogicalExpr>(&tree_.expressions[root->value].expression);
    const auto rhs = std::get<analysis::ValueId>(region.right);
    const auto producer = std::get<analysis::OpResult>(graph_.values[rhs.value].origin).op;
    Require(logical && logical->operation == region.operation && flow_.expressions[producer.value] == logical->right, Error::kPlacement);
    flow_.logical_operations[region.boolean.value] = flow_.logical_operations[region.branch.value] = region.operation;
    LogicalTruths(region,logical->left);
    LogicalEdges(region, {}, logical->left, logical->right, parent, child);
    LogicalPrefixes(region, parent, child);
  }
  void LogicalTruths(const SelectionRegion& region,ExprId input) {
    for(const auto& test:region.tests) {
      budget_.Step();Require(!flow_.selection_truths[test.operation.value],Error::kPlacement);
      flow_.selection_truths[test.operation.value]=TreeSelectionTruth{
          region.boolean,region.left,input,graph_.ops[region.boolean.value].first_use,test.meaning};
    }
  }
  void LogicalPrefixes(const SelectionRegion& region, StmtId parent, std::uint32_t child) {
    for (const auto branch : region.arm_prefixes) {
      budget_.Step(); Require(logical_branches_[branch.value].has_value(), Error::kPlacement);
      const auto& prefix = logicals_[*logical_branches_[branch.value]];
      Require(prefix.consequent && prefix.prefix_parent == region.branch && flow_.expressions[branch.value] &&
          evaluation_.operations[branch.value].kind == EvaluationKind::kLogicalValue, Error::kPlacement);
      Conditional(prefix, *flow_.expressions[branch.value], {}, parent, child);
    }
  }
  void LogicalEdges(const SelectionRegion& region, std::optional<SymbolId> target, ExprId left, ExprId right,
      StmtId parent, std::uint32_t child) {
    if (region.keep_input) edges_->Select(*region.keep_input, region.merged, target, left);
    if (region.evaluate_input) edges_->Select(*region.evaluate_input, region.merged, target, right);
    const auto anchors = [&](analysis::WorkBlockId id) {
      const auto& block = graph_.blocks[id.value];
      for (std::uint32_t j = 0; j < block.op_count; ++j) {
        budget_.Step(); const auto at = block.first_op.value + j; const auto& op = graph_.ops[at];
        if (op.life != analysis::ValueLife::kLive || !std::holds_alternative<analysis::ValueJump>(op.operation)) continue;
        const auto* anchor = std::get_if<AnchorId>(&tree_.operations[*placements_[at]].node);
        Require(anchor != nullptr, Error::kPlacement);
        const auto* boundary = std::get_if<BlockBoundary>(&sequences_.Anchor(*anchor));
        Require(boundary && boundary->block == parent && boundary->index == child, Error::kSequence);
      }
    };
    const auto path_anchors = [&](std::optional<ControlId> path, analysis::WorkBlockId terminal) {
      if (!path) { anchors(terminal); return; }
      while (path) {
        budget_.Step(); const auto& part = control_.nodes[path->value];
        anchors(SourceBlock(part)); path = part.next;
      }
    };
    if (region.keep) path_anchors(region.keep_path, *region.keep);
    path_anchors(region.evaluate_path, region.evaluate);
  }
  void Try(analysis::TryId id, const BlockStmt& parent, std::uint32_t& child,
      std::size_t& pending) {
    Require(child < parent.child_count && !flow_.tries[id.value], Error::kSequence);
    const auto statement = sequences_.Child(parent.first_child + child++);
    const auto& node = tree_.statements[statement.value];
    const auto* syntax = std::get_if<TryStmt>(&node.statement);
    const auto* origin = std::get_if<TryOrigin>(&tree_.origins[node.origin.value]);
    Require(syntax && syntax->handler && !syntax->finalizer && origin && origin->region == id,
        Error::kControl);
    const auto& planned = *exceptions_->control.regions[id.value];
    const auto& region = exceptions_->regions.regions[id.value];
    Require(region.binding.has_value(), Error::kPlacement);
    const auto parameter = syntax->handler->parameter;
    const auto& symbol = tree_.symbols[parameter.value];
    const auto& body = sequences_.Block(syntax->body);
    const auto& handler = sequences_.Block(syntax->handler->body);
    const auto parent_scope = sequences_.TryParent(statement, parent.scope);
    Require(tree_.statements[syntax->body.value].origin == node.origin &&
        tree_.statements[syntax->handler->body.value].origin == node.origin &&
        tree_.scopes[body.scope.value].parent == parent_scope &&
        tree_.scopes[body.scope.value].kind == ScopeKind::kBlock &&
        tree_.scopes[handler.scope.value].parent == parent_scope &&
        tree_.scopes[handler.scope.value].kind == ScopeKind::kCatch &&
        symbol.scope == handler.scope && symbol.kind == BindingKind::kCatchParameter &&
        symbol.declaration == DeclarationKind::kCatchParameter && !symbol.temporary &&
        !declared_[parameter.value] && !assigned_[parameter.value], Error::kPlacement);
    declared_[parameter.value] = 1;
    auto& binding = flow_.catch_parameters[region.binding->value.value];
    Require(!binding, Error::kPlacement);
    binding = parameter;
    flow_.tries[id.value] = statement;
    Require(tasks_.size() - pending >= 2, Error::kResourceLimit);
    tasks_[pending++] = {planned.handler, syntax->handler->body, 0};
    tasks_[pending++] = {planned.body, syntax->body, 0};
  }
  void ModulePairs() {
    for (const auto& pair : evaluation_.guarded_reads) {
      budget_.Step();
      Require(flow_.expressions[pair.read.value] &&
          flow_.expressions[pair.read.value] == flow_.expressions[pair.guard.value], Error::kPlacement);
    }
    for (const auto& module : evaluation_.modules) {
      budget_.Step();
      const auto read = module.pair.read.value;
      Require(flow_.expressions[read].has_value(), Error::kPlacement);
      if (!module.pair.guard) continue;
      const auto guard = module.pair.guard->value;
      // The evaluation checker proved exact guarded adjacency and effect order.
      // Two PC placements own ONE expression occurrence, not two executions.
      Require(flow_.expressions[read] && flow_.expressions[guard] == flow_.expressions[read], Error::kPlacement);
    }
  }
  void CapturedCells() {
    std::size_t next = 0;
    std::optional<SymbolId> inner;
    if (tree_.class_scope) {
      VerifyClassInnerScope(tree_, budget_);
      for (std::uint32_t s = 0; s < tree_.symbols.size(); ++s) {
        budget_.Step();
        if (tree_.symbols[s].kind == BindingKind::kClassInner) {
          Require(!inner, Error::kPlacement);
          inner = SymbolId{s};
        }
      }
    }
    if (evaluation_.cells) {
      const auto& cells = evaluation_.cells->cells.cells;
      for (std::size_t i = 0; i < cells.size(); ++i) {
        budget_.Step();
        if (!std::holds_alternative<IncomingCell>(cells[i].storage)) continue;
        const auto incoming = std::get<IncomingCell>(cells[i].storage);
        if (inner) {
          const auto& requirements = tree_.class_scope->Scope().requirements;
          const auto key =
              std::tuple{tree_.class_scope->Owner().family,
                         tree_.class_scope->Owner().member,
                         incoming.depth.value, incoming.slot.value};
          const auto found = std::lower_bound(
              requirements.begin(), requirements.end(), key,
              [&](const auto& r, auto k) {
                budget_.Step();
                return std::tuple{r.owner.family, r.owner.member,
                                  r.incoming.depth.value,
                                  r.incoming.slot.value} < k;
              });
          Require(found != requirements.end() &&
                      std::tuple{found->owner.family, found->owner.member,
                                 found->incoming.depth.value,
                                 found->incoming.slot.value} == key,
                  Error::kPlacement);
          if (found->kind == ClassSelfRequirementKind::kClassInner) {
            const auto symbol = *inner;
            // Member trees retain their checked physical capture transport;
            // initializer trees have no such entry. Neither is a source read
            // of the creator's later public/cache binding.
            if (next < tree_.captures.size() && tree_.captures[next].symbol == symbol)
              ++next;
            Require(!declared_[symbol.value], Error::kPlacement);
            declared_[symbol.value] = 1;
            flow_.cells[i] = symbol;
            flow_.cell_bindings[i] = {CellStorageIdentity(cells[i].storage),
                                      symbol,
                                      tree_.symbols[symbol.value].scope,
                                      {}};
            continue;
          }
        }
        Require(evaluation_.cells->incoming == IncomingCells::kConditional && next < tree_.captures.size(), Error::kPlacement);
        const auto symbol = tree_.captures[next++].symbol;
        Require(symbol.value < tree_.symbols.size(), Error::kPlacement);
        const auto& binding = tree_.symbols[symbol.value];
        Require(binding.kind == BindingKind::kCapture && tree_.capture_scope == binding.scope &&
            binding.declaration == DeclarationKind::kLet && !binding.temporary && !declared_[symbol.value], Error::kPlacement);
        declared_[symbol.value] = 1;
        flow_.cells[i] = symbol;
        flow_.cell_bindings[i]={CellStorageIdentity(cells[i].storage),symbol,binding.scope,{}};
      }
    }
    Require(next == tree_.captures.size(), Error::kPlacement);
  }
  void ModuleAliases() {
    for (const auto& alias : tree_.modules) {
      budget_.Step();
      Require(alias.symbol.value < tree_.symbols.size() && !declared_[alias.symbol.value] &&
          tree_.symbols[alias.symbol.value].kind == (alias.local ? BindingKind::kLocal : BindingKind::kModule) &&
          tree_.symbols[alias.symbol.value].scope == (alias.local ? tree_.body_scope : *tree_.module_scope), Error::kPlacement);
      if (!alias.local) declared_[alias.symbol.value] = 1;
    }
  }
  analysis::LexicalBinding CellStorageIdentity(const CellStorage& storage) {
    if (const auto* incoming = std::get_if<IncomingCell>(&storage))return {incoming->depth,incoming->slot};
    const auto& local = std::get<AllocatedCell>(storage);
    Require(evaluation_.cells && evaluation_.cells->environments,Error::kEvaluation);
    const auto& states = *evaluation_.cells->environments;
    Require(local.allocation.value < states.normal_after.size() &&
        states.normal_after[local.allocation.value].has_value(),Error::kEvaluation);
    // CellPlan proves the allocation/slot; this source binding consumes the
    // common success transition, never a guessed scope-entry allocation.
    return {*states.normal_after[local.allocation.value],local.slot};
  }
  // The cell whose deferred TDZ initialization is this exact first store.
  std::optional<CellId> DeferredStore(analysis::OpId id) const {
    if (!evaluation_.cells || id.value >= evaluation_.cells->cells.operations.size()) return {};
    const auto cell = evaluation_.cells->cells.operations[id.value];
    const auto& deferred = evaluation_.cells->deferred;
    if (!cell || cell->value >= deferred.size() || deferred[cell->value] != id) return {};
    return cell;
  }
  // `let x = value` at the original first store. A smaller phased source
  // scope needs the independent private activation/enclosure proof.
  void DeferredDeclaration(analysis::OpId id, CellId cell, StmtId site, const BlockStmt& block, StmtId owner) {
    Require(cell.value < deferred_blocks_.size() && deferred_blocks_[cell.value].has_value(), Error::kPlacement);
    if (!exceptions_) Require(deferred_blocks_[cell.value] == owner, Error::kPlacement);
    else {
      Require(deferred_scopes_ && id.value < graph_.ops.size(), Error::kPlacement);
      const auto expected = std::get<CellScopeId>(deferred_scopes_->cells[cell.value]);
      const auto at = deferred_scopes_->blocks[graph_.ops[id.value].block.value];
      Require(at == expected, Error::kPlacement);
      if (!deferred_scope_blocks_[expected.value]) deferred_scope_blocks_[expected.value] = owner;
      Require(deferred_scope_blocks_[expected.value] == owner, Error::kPlacement);
    }
    LocationScope witness(*this, At(id)); location_.statement = site;
    const auto& statement = tree_.statements[site.value];
    const auto* declaration = std::get_if<DeclarationStmt>(&statement.statement);
    Require(declaration && declaration->initializer && declaration->symbol.value < tree_.symbols.size() &&
        statement.origin.value == graph_.ops[id.value].origin.value, Error::kPlacement);
    const auto symbol = declaration->symbol;
    const auto& binding = tree_.symbols[symbol.value];
    Require(binding.kind == BindingKind::kLocal && binding.scope == block.scope &&
        binding.declaration == DeclarationKind::kLet && !binding.temporary &&
        !declared_[symbol.value], Error::kSnapshot);
    auto& actual = flow_.cells[cell.value];
    Require(!actual, Error::kPlacement);
    declared_[symbol.value] = 1;
    actual = symbol;
    const auto& storage = evaluation_.cells->cells.cells[cell.value].storage;
    flow_.cell_bindings[cell.value] = {CellStorageIdentity(storage), symbol, binding.scope, site, {}};
    flow_.expressions[id.value] = *declaration->initializer;
  }
  void CellDeclarations(analysis::OpId allocation, const BlockStmt& block, std::uint32_t& child, StmtId owner) {
    Require(evaluation_.cells.has_value(), Error::kEvaluation);
    if (!std::holds_alternative<analysis::ValueEnvironmentPush>(graph_.ops[allocation.value].operation)) return;
    const auto& cells = evaluation_.cells->cells.cells;
    auto cell = std::lower_bound(cells.begin(), cells.end(), allocation,
        [&](const Cell& item, analysis::OpId site) {
          budget_.Step();
          const auto* local = std::get_if<AllocatedCell>(&item.storage);
          return local && local->allocation < site;
        });
    std::optional<StmtId> activation;
    for (; cell != cells.end(); ++cell) {
      budget_.Step();
      const auto* local = std::get_if<AllocatedCell>(&cell->storage);
      if (!local || local->allocation != allocation) break;
      const auto index = static_cast<std::size_t>(cell - cells.begin());
      if (index < evaluation_.cells->deferred.size() && evaluation_.cells->deferred[index]) {
        // No declaration here: its first store directly in this same block
        // statement declares it (DeferredDeclaration) and binds its symbol.
        Require(!flow_.cells[index], Error::kPlacement);
        if (deferred_blocks_.empty()) budget_.Array(deferred_blocks_, cells.size());
        Require(!deferred_blocks_[index], Error::kPlacement);
        deferred_blocks_[index] = owner;
        continue;
      }
      Require(child < block.child_count, Error::kSequence);
      const auto site=sequences_.Child(block.first_child + child++);
      if(!activation)activation=site;
      const auto& statement = tree_.statements[site.value];
      const auto* declaration = std::get_if<DeclarationStmt>(&statement.statement);
      Require(declaration && !declaration->initializer &&
          statement.origin.value == graph_.ops[allocation.value].origin.value, Error::kPlacement);
      const auto symbol = declaration->symbol;
      const auto& binding = tree_.symbols[symbol.value];
      Require(binding.kind == BindingKind::kLocal && binding.scope == block.scope &&
          binding.declaration == DeclarationKind::kLet && !binding.temporary &&
          !declared_[symbol.value], Error::kSnapshot);
      declared_[symbol.value] = 1;
      auto& actual = flow_.cells[static_cast<std::size_t>(cell - cells.begin())];
      Require(!actual, Error::kPlacement);
      actual = symbol;
      flow_.cell_bindings[static_cast<std::size_t>(cell-cells.begin())]={CellStorageIdentity(cell->storage),symbol,binding.scope,site,OriginalCellActivation{allocation,*activation}};
    }
  }
  void ExpressionScopes() {
    if (!exceptions_) return;
    bool needed = ArrayInitializations(evaluation_) != nullptr;
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      budget_.Step(); needed |= (evaluation_.operations[i].kind == EvaluationKind::kPhiInput ||
          ((!original_prefixes_.empty() && original_prefixes_[i]) ||
           (i < evaluation_.phase_prefixes.size() && evaluation_.phase_prefixes[i]))) && OrderedEvaluation(graph_.ops[i]);
    }
    if (evaluation_.literals) for (const auto& object : evaluation_.literals->objects) {
      budget_.Step(); needed |= object.form == LiteralForm::kExpression &&
          graph_.ops[object.allocation.value].exceptional == analysis::ExceptionalCompletion::kImpossible;
    }
    if (!needed) return;
    // Reuse the independently traversed AST scopes and original try mapping.
    // Memoize nearest protected/handler scope once, not a parent walk per field.
    std::vector<std::optional<analysis::CatchScope>> scopes;
    std::vector<std::uint8_t> resolved;
    std::vector<ScopeId> path;
    budget_.Array(scopes, tree_.scopes.size()); budget_.Array(resolved, tree_.scopes.size());
    budget_.Array(path, tree_.scopes.size());
    const auto seed = [&](ScopeId scope, analysis::TryId region, analysis::CatchPart part) {
      Require(!resolved[scope.value], Error::kControl);
      scopes[scope.value] = analysis::CatchScope{region, part}; resolved[scope.value] = 1;
    };
    for (std::size_t r = 0; r < flow_.tries.size(); ++r) {
      budget_.Step(); if (exceptions_->regions.layout.regions[r].inactive ||
          (enumeration_ && enumeration_->forwarded_regions[r]) ||
          (iteration_ && (exceptions_->control.region_roles[r]==ExceptionRegionRole::kProtocolOwned || exceptions_->control.region_roles[r]==ExceptionRegionRole::kForwardedTry))) continue;
      Require(flow_.tries[r].has_value(), Error::kControl);
      const auto& syntax = std::get<TryStmt>(tree_.statements[flow_.tries[r]->value].statement);
      Require(syntax.handler.has_value(), Error::kControl);
      const analysis::TryId region{static_cast<std::uint32_t>(r)};
      seed(sequences_.Block(syntax.body).scope, region, analysis::CatchPart::kProtected);
      seed(sequences_.Block(syntax.handler->body).scope, region, analysis::CatchPart::kHandler);
    }
    for (std::size_t s = 0; s < scopes.size(); ++s) {
      budget_.Step(); if (resolved[s]) continue;
      std::size_t count = 0;
      std::optional<ScopeId> cursor{ScopeId{static_cast<std::uint32_t>(s)}};
      while (cursor && !resolved[cursor->value]) {
        budget_.Step(); Require(count < path.size(), Error::kControl);
        path[count++] = *cursor; cursor = tree_.scopes[cursor->value].parent;
      }
      const auto parent = cursor ? scopes[cursor->value] : std::nullopt;
      while (count) {
        budget_.Step(); const auto scope = path[--count];
        scopes[scope.value] = parent; resolved[scope.value] = 1;
      }
    }
    std::vector<std::optional<analysis::CatchScope>> retained_parents;
    if(iteration_) {
      budget_.Array(retained_parents,exceptions_->regions.layout.regions.size());
      for(const auto r:exceptions_->regions.layout.source_order) {
        budget_.Step();auto parent=exceptions_->regions.layout.regions[r.value].parent;
        if(parent && exceptions_->control.region_roles[parent->region.value]!=ExceptionRegionRole::kOrdinaryTry)
          parent=retained_parents[parent->region.value];
        retained_parents[r.value]=parent;
      }
    }
    const auto check = [&](analysis::OpId operation, ExprId expression) {
      budget_.Step();
      const auto site = graph_.phases[graph_.ops[operation.value].block.value].instruction.value;
      auto expected = exceptions_->regions.layout.scopes[site];
      if(expected && iteration_ && exceptions_->control.region_roles[expected->region.value]!=ExceptionRegionRole::kOrdinaryTry)
        expected=retained_parents[expected->region.value];
      const auto& actual = scopes[expression_scopes_[expression.value].value];
      Require(expected.has_value() == actual.has_value() &&
          (!expected || (expected->region == actual->region && expected->part == actual->part)), Error::kPlacement);
    };
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      budget_.Step();
      const auto kind = evaluation_.operations[i].kind;
      if ((kind == EvaluationKind::kPhiInput || kind == EvaluationKind::kInline ||
          ((!original_prefixes_.empty() && original_prefixes_[i]) ||
           (i < evaluation_.phase_prefixes.size() && evaluation_.phase_prefixes[i]))) && OrderedEvaluation(graph_.ops[i])) {
        Require(flow_.expressions[i].has_value(), Error::kPlacement);
        check({static_cast<std::uint32_t>(i)}, *flow_.expressions[i]);
      }
    }
    if (!evaluation_.literals) return;
    if (const auto* arrays = ArrayInitializations(evaluation_)) for (const auto& array : arrays->arrays) {
      check(array.allocation, *flow_.expressions[array.allocation.value]);
      for (std::uint32_t i = 0; i < array.count; ++i) {
        const auto& step = arrays->steps[array.first + i];
        if (step.kind == ArrayStepKind::kSpread) check(step.operation, *flow_.expressions[step.operation.value]);
      }
    }
    for (const auto& object : evaluation_.literals->objects) {
      budget_.Step(); if (object.form != LiteralForm::kExpression) continue;
      check(object.allocation, *flow_.expressions[object.allocation.value]);
      for (std::size_t d = object.first; d < std::size_t(object.first) + object.count; ++d) {
        const auto id = evaluation_.literals->definitions[d];
        check(id, tree_.properties[flow_.initializers[id.value]->value].value);
      }
    }
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      budget_.Step(); if (evaluation_.operations[i].kind == EvaluationKind::kNestedLiteral)
        check({static_cast<std::uint32_t>(i)}, *flow_.expressions[i]);
    }
    for (const auto& module : evaluation_.modules) {
      budget_.Step(); if (NestedLiteralUse(evaluation_, module.pair.read) && module.pair.guard)
        check(*module.pair.guard, *flow_.expressions[module.pair.guard->value]);
    }
  }
  void Arrays() {
    std::size_t arrays_seen = 0, elements_seen = 0;
    if (const auto* proof = ArrayInitializations(evaluation_)) for (const auto& array : proof->arrays) {
      budget_.Step(); const auto expression = flow_.expressions[array.allocation.value];
      Require(expression.has_value(), Error::kPlacement);
      const auto* syntax = std::get_if<ArrayExpr>(&tree_.expressions[expression->value].expression);
      Require(syntax && syntax->buffered == array.buffered && syntax->elements.count >= array.buffered, Error::kPlacement);
      ++arrays_seen; std::uint32_t next = array.buffered;
      for (std::uint32_t i = 0; i < array.buffered; ++i) {
        budget_.Step(); const auto id = tree_.arguments[syntax->elements.first + i];
        const auto& node = tree_.expressions[id.value];
        const auto* element = std::get_if<ArrayElement>(&node.expression);
        Require(element && std::holds_alternative<ArrayValueElement>(element->element) &&
            node.origin.value == graph_.ops[array.allocation.value].origin.value, Error::kPlacement);
      }
      for (std::uint32_t i = 0; i < array.count; ++i) {
        budget_.Step(); const auto& step = proof->steps[array.first + i];
        if (!step.value) continue;
        Require(next < syntax->elements.count && flow_.expressions[step.operation.value] ==
            tree_.arguments[syntax->elements.first + next], Error::kPlacement);
        const auto& node = tree_.expressions[tree_.arguments[syntax->elements.first + next++].value];
        const auto* element = std::get_if<ArrayElement>(&node.expression);
        Require(element && node.origin.value == graph_.ops[step.operation.value].origin.value &&
            (step.kind == ArrayStepKind::kSpread ? std::holds_alternative<ArraySpreadElement>(element->element) :
                std::holds_alternative<ArrayValueElement>(element->element)), Error::kPlacement);
      }
      Require(next == syntax->elements.count, Error::kPlacement); elements_seen += next;
    }
    for (const auto& node : tree_.expressions) {
      budget_.Step();
      if (std::holds_alternative<ArrayExpr>(node.expression)) { Require(arrays_seen != 0, Error::kPlacement); --arrays_seen; }
      if (std::holds_alternative<ArrayElement>(node.expression)) { Require(elements_seen != 0, Error::kPlacement); --elements_seen; }
    }
    Require(arrays_seen == 0 && elements_seen == 0, Error::kPlacement);
  }
  void Objects() {
    std::size_t count = 0;
    std::vector<std::uint8_t> grouped;
    budget_.Array(grouped, graph_.ops.size());
    if (evaluation_.literals) for (const auto& object : evaluation_.literals->objects) {
      budget_.Step(); if (object.form == LiteralForm::kStatements &&
          (evaluation_.logical_objects.empty() || !evaluation_.logical_objects[object.allocation.value])) continue;
      const auto expression = flow_.expressions[object.allocation.value];
      Require(expression.has_value(), Error::kPlacement);
      const auto* syntax = std::get_if<ObjectExpr>(&tree_.expressions[expression->value].expression);
      Require(syntax && syntax->properties.count >= object.count, Error::kPlacement);
      const auto* allocation = std::get_if<analysis::ValueObjectAllocation>(&graph_.ops[object.allocation.value].operation);
      Require(syntax->kind == (allocation ? AggregateKind::kObject : AggregateKind::kArray), Error::kPlacement);
      const auto* array = std::get_if<analysis::ValueArrayAllocation>(&graph_.ops[object.allocation.value].operation);
      Require((allocation && allocation->literal) || (array && array->literal) || syntax->buffered == 0, Error::kPlacement);
      Require(!grouped[object.allocation.value], Error::kPlacement);
      grouped[object.allocation.value] = 1;
      std::uint32_t next = syntax->properties.first;
      for (std::uint32_t i = 0; i < object.count; ++i) {
        budget_.Step();
        const auto id = evaluation_.literals->definitions[object.first + i];
        const auto actual = flow_.initializers[id.value];
        // Exact, unique placements in increasing property order preserve every
        // own-definition RHS evaluation, including nested allocation identity.
        Require(actual && actual->value >= next &&
            actual->value < syntax->properties.first + syntax->properties.count, Error::kPlacement);
        next = actual->value + 1;
        Require(tree_.properties[actual->value].origin.value == graph_.ops[id.value].origin.value, Error::kPlacement);
        if(id.value<evaluation_.literal_fields.fields.size()) {
          const auto range=evaluation_.literal_fields.fields[id.value];
          auto value=tree_.properties[actual->value].value;
          for(std::size_t p=0;p<range.count;++p){
            budget_.Step();const auto root=evaluation_.literal_fields.prefixes[range.first+p];
            const auto& node=tree_.expressions[value.value];const auto* sequence=std::get_if<SequenceExpr>(&node.expression);
            Require(sequence&&node.origin.value==graph_.ops[id.value].origin.value,Error::kSequence);
            if(std::holds_alternative<analysis::ValueBranch>(graph_.ops[root.value].operation)) {
              const auto* assignment=std::get_if<AssignmentExpr>(&tree_.expressions[sequence->left.value].expression);
              Require(assignment&&flow_.expressions[root.value]==assignment->value,Error::kPlacement);
              const auto* target=std::get_if<SymbolTarget>(&assignment->target);
              const auto index=logical_branches_[root.value];Require(target&&index,Error::kPlacement);
              const auto merged=logicals_[*index].merged;
              Require(flow_.phis[merged.value]==target->symbol,Error::kSnapshot);
            } else RootExpression(root,sequence->left);
            value=sequence->right;
          }
        }
      }
      for (std::uint32_t p = syntax->buffered; p < syntax->properties.count; ++p) {
        budget_.Step();
        Require(flow_.defined_properties[syntax->properties.first + p], Error::kPlacement);
      }
      count += syntax->properties.count;
    }
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      budget_.Step(); const auto& op = graph_.ops[i];
      if (op.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::ValueIterResult>(op.operation)) {
        Require(flow_.expressions[i].has_value(), Error::kPlacement);
        const auto* object = std::get_if<ObjectExpr>(&tree_.expressions[flow_.expressions[i]->value].expression);
        Require(object && object->kind == AggregateKind::kObject && object->buffered == 0 &&
            object->properties.count == IterResultConstant::kProperties, Error::kPlacement);
        for (std::uint32_t p = 0; p < object->properties.count; ++p) {
          budget_.Step(); const auto index = object->properties.first + p;
          Require(!flow_.defined_properties[index] &&
              tree_.properties[index].origin.value == op.origin.value, Error::kPlacement);
        }
        count += object->properties.count;
        continue;
      }
      if (ArrayLiteralFor(evaluation_, {static_cast<std::uint32_t>(i)})) continue;
      const auto* allocation = std::get_if<analysis::ValueObjectAllocation>(&op.operation);
      const auto* array = std::get_if<analysis::ValueArrayAllocation>(&op.operation);
      if (op.life != analysis::ValueLife::kLive || !((allocation && allocation->literal) || (array && array->literal))) continue;
      Require(flow_.expressions[i].has_value(), Error::kPlacement);
      const auto* object = std::get_if<ObjectExpr>(&tree_.expressions[flow_.expressions[i]->value].expression);
      Require(object && object->kind == (array ? AggregateKind::kArray : AggregateKind::kObject), Error::kPlacement);
      const auto prefix = object->buffered;
      flow_.buffered_properties[i] = prefix;
      if (!grouped[i]) {
        Require(object->properties.count == prefix, Error::kPlacement);
        count += prefix;
      }
      for (std::uint32_t p = 0; p < prefix; ++p) {
        budget_.Step();
        const auto index = object->properties.first + p;
        if (flow_.defined_properties[index]) { Require(grouped[i], Error::kPlacement); continue; }
        const auto& property = tree_.properties[index];
        Require(property.origin.value == op.origin.value &&
            tree_.expressions[property.value.value].origin == property.origin, Error::kPlacement);
      }
    }
    Require(count == tree_.properties.size(), Error::kPlacement);
  }
  void IterationPayload(const ForOfExitPayload& expected,ExprId id) {
    Require(id.value<tree_.expressions.size(),Error::kPlacement);
    if(const auto* v=std::get_if<analysis::ValueId>(&expected)) {
      const auto* symbol=std::get_if<SymbolExpr>(&tree_.expressions[id.value].expression);Require(symbol,Error::kPlacement);
      const auto value=evaluation_.aliases.empty()?*v:evaluation_.aliases[v->value];
      const auto& origin=graph_.values[value.value].origin;std::optional<SymbolId> binding;
      if(const auto* result=std::get_if<analysis::OpResult>(&origin))binding=flow_.snapshots[result->op.value];
      else if(std::holds_alternative<analysis::PhiDefinition>(origin))binding=flow_.phis[value.value];
      else if(const auto* entry=std::get_if<analysis::EntryDefinition>(&origin))if(const auto* parameter=std::get_if<analysis::ParameterEntry>(entry)) {
        Require(parameter->index<tree_.parameters.size(),Error::kPlacement);binding=tree_.parameters[parameter->index].symbol;
      }
      Require(binding && *binding==symbol->symbol,Error::kPlacement);return;
    }
    const auto* literal=std::get_if<TreeLiteral>(&tree_.expressions[id.value].expression);Require(literal,Error::kPlacement);
    Require(std::holds_alternative<ForOfUndefined>(expected) && std::holds_alternative<UndefinedLiteral>(*literal),Error::kPlacement);
  }
  void Iteration(analysis::OpId id,const BlockStmt& block,std::uint32_t& child,bool commit) {
    const auto member=*evaluation_.for_of->operations[id.value];const auto i=member.protocol.value;
    Require(iteration_statements_[i].has_value(),Error::kPlacement);const auto statement=*iteration_statements_[i];
    const auto& syntax=std::get<ForOfStmt>(tree_.statements[statement.value].statement);const auto& view=iteration_->loops[i];
    if(member.role==ForOfRole::kAcquisition) {
      Require(syntax.binding==ForOfBinding::kAssignment && tree_.statements[statement.value].origin.value==graph_.ops[id.value].origin.value,Error::kPlacement);
      const auto& symbol=tree_.symbols[syntax.target.value];
      Require(symbol.kind==BindingKind::kCompilerTemporary && symbol.declaration==DeclarationKind::kLet && symbol.scope==tree_.body_scope && symbol.temporary==TemporaryReason::kEnumerationKey && declared_[syntax.target.value] && !assigned_[syntax.target.value],Error::kSnapshot);
      assigned_[syntax.target.value]=1;flow_.snapshots[evaluation_.for_of->identity.loops[i].value.value]=syntax.target;
      iteration_scopes_[i]=block.scope;flow_.for_of->statements[i]=statement;
      Require(!loops_[view.loop.value],Error::kControl);loops_[view.loop.value]=statement;
      const auto operand=ResolveEvaluationOperand(evaluation_,graph_.uses[view.iterable.value].operand);
      const auto* value=std::get_if<analysis::ValueId>(&operand);
      // The common evaluation plan materializes effectful RHS producers at
      // their original roots. Check the actual leaf, not its builder ExprId.
      Require(value!=nullptr,Error::kPlacement);IterationPayload(ForOfExitPayload{*value},syntax.iterable);
      flow_.expressions[id.value]=syntax.iterable;edges_->Stage(id,block,child);
    }
    if(member.role==ForOfRole::kDecision && commit)edges_->Consume(view.success,block,child);
  }
  void Enumeration(analysis::OpId id, const BlockStmt& block, std::uint32_t& child, bool commit) {
    const auto member = *evaluation_.for_in->operations[id.value];
    const auto& protocol = evaluation_.for_in->loops[member.loop];
    const auto& event = evaluation_.for_in_events->loops[member.loop];
    const auto statement = *enumeration_statements_[member.loop];
    const auto& syntax = std::get<ForInStmt>(tree_.statements[statement.value].statement);
    if (id == protocol.acquisition) {
      Require(syntax.binding == ForInBinding::kAssignment &&
          tree_.statements[statement.value].origin.value == graph_.ops[id.value].origin.value, Error::kSequence);
      const auto key = syntax.key; const auto& symbol = tree_.symbols[key.value];
      Require(symbol.kind == BindingKind::kCompilerTemporary && symbol.declaration == DeclarationKind::kLet &&
          symbol.scope == tree_.body_scope && symbol.temporary == TemporaryReason::kEnumerationKey &&
          declared_[key.value] && !assigned_[key.value], Error::kSnapshot);
      assigned_[key.value] = 1; enumeration_scopes_[member.loop] = block.scope;
      flow_.enumerations[member.loop] = {event, statement, syntax.object, key, block.scope,
          enumeration_->loops[member.loop].acquisition, event.key.success, event.key.exhausted};
      const auto loop = enumeration_->loops[member.loop].loop;
      Require(!loops_[loop.value], Error::kControl); loops_[loop.value] = statement;
      // Stage is still checked at the original acquisition, not at the loop's
      // header or a body call. Forwarding never invents catch snapshots.
      edges_->Stage(id, block, child);
    }
    if (member.role == ForInRole::kDecision && commit) {
      if (!enumeration_->loops[member.loop].exhaustion_only) {
        Require(!edges_->HasCopies(event.key.exhausted), Error::kSequence);
        edges_->Consume(event.key.exhausted, block, child);
      }
      edges_->Consume(event.key.success, block, child);
    }
  }
  void Loop(ControlId id, const BlockStmt& block, std::uint32_t& child,
      std::size_t& pending) {
    Require(child < block.child_count, Error::kSequence);
    const auto statement = sequences_.Child(block.first_child + child++);
    const auto& syntax = tree_.statements[statement.value];
    const auto& node = control_.nodes[id.value];
    const auto first = exceptions_ ? exceptions_->control.units.size() : graph_.blocks.size();
    Require(id.value >= first && id.value - first < flow_.controls.size(), Error::kControl);
    flow_.controls[id.value - first] = syntax.origin;
    flow_.control_statements[id.value - first] = statement;
    if (const auto* wrapper = std::get_if<LoopControl>(&node.synthetic)) {
      const auto& loop = control_.loops[wrapper->loop.value];
      const auto* body = std::get_if<LoopStmt>(&syntax.statement);
      const auto* origin = std::get_if<LoopOrigin>(&tree_.origins[syntax.origin.value]);
      const auto header = exceptions_ ? ExceptionNodeBlock(*exceptions_, {loop.header.value}) :
          std::optional<analysis::WorkBlockId>{loop.header};
      Require(body && origin && header && origin->header == *header &&
          tree_.statements[body->body.value].origin == syntax.origin &&
          !loops_[wrapper->loop.value], Error::kControl);
      loops_[wrapper->loop.value] = statement;
      Require(pending < tasks_.size(), Error::kResourceLimit);
      tasks_[pending++] = {loop.body, body->body, 0};
      return;
    }
    const auto& transfer = std::get<LoopTransfer>(node.synthetic);
    const auto target = loops_[transfer.loop.value];
    Require(target.has_value(), Error::kControl);
    switch (transfer.kind) {
      case LoopTransferKind::kBreak: {
        const auto* jump = std::get_if<BreakStmt>(&syntax.statement);
        Require(jump && jump->target == *target, Error::kControl);
        break;
      }
      case LoopTransferKind::kContinue: {
        const auto* jump = std::get_if<ContinueStmt>(&syntax.statement);
        Require(jump && jump->target == *target, Error::kControl);
        break;
      }
    }
    const auto& origin = tree_.origins[syntax.origin.value];
    if (const auto* edge = std::get_if<analysis::EdgeId>(&transfer.source)) {
      auto expected = exceptions_ ? ExceptionArcOrigin(*exceptions_, {edge->value}) :
          std::optional<TreeOrigin>{EdgeOrigin{*edge}};
      if(!expected && iteration_ && edge->value<control_.edges.size()) {
        const auto owner=control_.edges[edge->value].owner;
        if(owner.value<iteration_->closes.size() && iteration_->closes[owner.value])expected=EdgeOrigin{iteration_->closes[owner.value]->edge};
      }
      Require(expected.has_value(), Error::kPlacement);
      if (const auto* original = std::get_if<EdgeOrigin>(&*expected)) {
        const auto* source = std::get_if<EdgeOrigin>(&origin);
        Require(source && source->edge == original->edge, Error::kPlacement);
      } else {
        const auto* source = std::get_if<TryOrigin>(&origin);
        Require(source && source->region == std::get<TryOrigin>(*expected).region, Error::kPlacement);
      }
    } else {
      const auto* source = std::get_if<LoopOrigin>(&origin);
      const auto projected = control_.loops[std::get<LoopId>(transfer.source).value].header;
      const auto header = exceptions_ ? ExceptionNodeBlock(*exceptions_, {projected.value}) :
          std::optional<analysis::WorkBlockId>{projected};
      Require(source && header && source->header == *header,
          Error::kPlacement);
    }
  }
  void CheckBodyOrigin() {
    // Independently reconstruct the retained entry operation. A tombstone
    // prologue PC must not be presented as owning all remaining body syntax.
    std::optional<ControlId> cursor = control_.entry;
    while (cursor) {
      budget_.Step();
      const auto& node = control_.nodes[cursor->value];
      if (exceptions_ && std::holds_alternative<std::monostate>(node.synthetic)) {
        const auto& unit = exceptions_->control.units[cursor->value];
        if (const auto* region = std::get_if<TryControlUnit>(&unit)) {
          cursor = exceptions_->control.regions[region->region.value]->body;
          continue;
        }
        Require(std::holds_alternative<PhaseControlUnit>(unit), Error::kPlacement);
      }
      if (std::holds_alternative<ControlForward>(node.synthetic)) { cursor = node.next; continue; }
      if (const auto* loop = std::get_if<LoopControl>(&node.synthetic)) {
        cursor = control_.loops[loop->loop.value].body;
        continue;
      }
      Require(std::holds_alternative<std::monostate>(node.synthetic), Error::kPlacement);
      const auto& block = graph_.blocks[SourceBlock(node).value];
      for (std::uint32_t i = 0; i < block.op_count; ++i) {
        budget_.Step();
        const auto& op = graph_.ops[block.first_op.value + i];
        if (op.life == analysis::ValueLife::kLive &&
            evaluation_.operations[block.first_op.value + i].kind != EvaluationKind::kElidedPure &&
            evaluation_.operations[block.first_op.value + i].kind != EvaluationKind::kAlias) {
          Require(tree_.statements[tree_.body.value].origin.value == op.origin.value,
                  Error::kPlacement);
          return;
        }
      }
      Require(!node.arms, Error::kControl);
      cursor = node.next;
    }
    throw Error::kPlacement;
  }
  void IndexPlacements() {
    for (std::size_t i = 0; i < tree_.operations.size(); ++i) {
      budget_.Step();
      const auto& placement = tree_.operations[i];
      LocationScope witness(*this, At(placement.operation));
      Require(placement.operation.value < graph_.ops.size(), Error::kPlacement);
      auto& entry = placements_[placement.operation.value];
      Require(!entry && graph_.ops[placement.operation.value].life == analysis::ValueLife::kLive, Error::kPlacement);
      entry = i;
    }
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      budget_.Step();
      LocationScope witness(*this, At(analysis::OpId{static_cast<std::uint32_t>(i)}));
      const auto& op = graph_.ops[i];
      if (op.life != analysis::ValueLife::kLive) continue;
      Require(placements_[i].has_value(), Error::kPlacement);
      const auto& placement = tree_.operations[*placements_[i]].node;
      if(iteration_)if(const auto member=evaluation_.for_of->operations[i]) {
        const auto l=member->protocol.value;const auto acquisition=evaluation_.for_of->identity.loops[l].acquisition;
        Require(placements_[acquisition.value].has_value(),Error::kPlacement);
        const auto* statement=std::get_if<StmtId>(&placement);
        const auto* owner=std::get_if<StmtId>(&tree_.operations[*placements_[acquisition.value]].node);
        Require(statement && owner && *statement==*owner && std::holds_alternative<ForOfStmt>(tree_.statements[owner->value].statement),Error::kPlacement);
        Require(!iteration_statements_[l] || iteration_statements_[l]==*owner,Error::kPlacement);
        iteration_statements_[l]=*owner;flow_.statements[i]=*owner;continue;
      }
      if (enumeration_) {
        const auto member = evaluation_.for_in->operations[i];
        const auto forwarded = enumeration_->forwarded_operations[i];
        if (member || forwarded) {
          const auto l = member ? member->loop : *forwarded;
          const auto acquisition = evaluation_.for_in->loops[l].acquisition;
          Require(placements_[acquisition.value].has_value(), Error::kPlacement);
          const auto* statement = std::get_if<StmtId>(&placement);
          const auto* owner = std::get_if<StmtId>(&tree_.operations[*placements_[acquisition.value]].node);
          Require(statement && owner && *statement == *owner &&
              std::holds_alternative<ForInStmt>(tree_.statements[owner->value].statement), Error::kPlacement);
          enumeration_statements_[l] = *owner; flow_.statements[i] = *owner;
          continue;
        }
      }
      // Class member installations retain a PC anchor, not an object-literal
      // PropertyId. Their class owner/body correspondence is checked separately.
      if (evaluation_.operations[i].kind == EvaluationKind::kCellGuard) {
        Require(evaluation_.cells && IsInitializedCellGuard(graph_, *evaluation_.cells,
            analysis::OpId{static_cast<std::uint32_t>(i)}) && std::holds_alternative<AnchorId>(placement), Error::kPlacement);
        continue;
      }
      if (IsReceiverGuard(tree_, analysis::OpId{static_cast<std::uint32_t>(i)})) {
        Require((tree_.callable == CallableKind::kDerivedConstructor ||
            (std::holds_alternative<analysis::ValueSuperCheck>(op.operation) &&
             std::get<analysis::ValueSuperCheck>(op.operation).kind == analysis::SuperCheckKind::kRequireInitialized)) &&
            std::holds_alternative<analysis::ValueSuperCheck>(op.operation) &&
            std::holds_alternative<AnchorId>(placement), Error::kPlacement);
        continue;
      }
      if (evaluation_.operations[i].kind == EvaluationKind::kClassProjection ||
          evaluation_.operations[i].kind == EvaluationKind::kArrayCursor) {
        Require(std::holds_alternative<AnchorId>(placement), Error::kPlacement);
        continue;
      }
      if (IsArrayInitialization(evaluation_, {static_cast<std::uint32_t>(i)})) {
        const auto* expression = std::get_if<ExprId>(&placement);
        Require(expression != nullptr, Error::kPlacement);
        flow_.expressions[i] = *expression; continue;
      }
      if (!IsReceiverField(graph_, op) && (!IsStagedLiteral(evaluation_.literals ? &*evaluation_.literals : nullptr, {static_cast<std::uint32_t>(i)}) || evaluation_.operations[i].kind == EvaluationKind::kInitializer) && (std::holds_alternative<analysis::ValueOwnWrite>(op.operation) || std::holds_alternative<analysis::ValueIndexedWrite>(op.operation))) {
        const auto* property = std::get_if<PropertyId>(&placement);
        Require(property != nullptr, Error::kPlacement);
        Require(!flow_.defined_properties[property->value], Error::kPlacement);
        flow_.defined_properties[property->value] = 1;
        flow_.initializers[i] = *property;
        flow_.expressions[i] = tree_.properties[property->value].value;
      } else if ((std::holds_alternative<analysis::ValueModuleWrite>(op.operation) ||
                  DeferredStore({static_cast<std::uint32_t>(i)})) &&
                 std::holds_alternative<StmtId>(placement)) {
        // Initializations own a declaration statement; updates own assignments.
      } else if (evaluation_.operations[i].kind == EvaluationKind::kLogicalValue ||
          decision_branches_[i] ||
          (!logical_branches_.empty() && logical_branches_[i] &&
           (logicals_[*logical_branches_[i]].parent || logicals_[*logical_branches_[i]].prefix_parent))) {
        const auto* expression = std::get_if<ExprId>(&placement);
        Require(expression != nullptr, Error::kPlacement); flow_.expressions[i] = *expression;
      } else if (exceptions_ && std::holds_alternative<analysis::ValueBranch>(op.operation) &&
          !logical_branches_.empty() && logical_branches_[i] && std::holds_alternative<ExprId>(placement)) {
        // The decision walk must consume this retained phi assignment at its
        // exact prefix position. Merely indexing it does not prove execution.
        Require(evaluation_.operations[i].kind == EvaluationKind::kRoot, Error::kPlacement);
        flow_.expressions[i] = std::get<ExprId>(placement);
      } else if (evaluation_.operations[i].kind==EvaluationKind::kConcurrentNotification) {
        Require(std::holds_alternative<AnchorId>(placement),Error::kPlacement);
      } else if (std::holds_alternative<analysis::ValueBranch>(op.operation) ||
          std::holds_alternative<analysis::ValueDebugger>(op.operation) ||
          std::holds_alternative<analysis::ValueThrow>(op.operation) ||
          std::holds_alternative<analysis::ValueReturn>(op.operation)) {
        Require(std::holds_alternative<StmtId>(placement), Error::kPlacement);
      } else if (IsAbsentHeritageTransport(graph_, evaluation_, op) ||
              std::holds_alternative<analysis::ValueJump>(op.operation) || IsEnvironmentState(op.operation) ||
          evaluation_.operations[i].kind == EvaluationKind::kElidedPure ||
          evaluation_.operations[i].kind == EvaluationKind::kAlias) {
        Require(std::holds_alternative<AnchorId>(placement), Error::kPlacement);
      } else {
        const auto* expression = std::get_if<ExprId>(&placement);
        Require(expression != nullptr, Error::kPlacement);
        flow_.expressions[i] = *expression;
      }
    }
  }
  std::uint32_t Declarations() {
    Require(tree_.hole.has_value() == NeedsHoleToken(evaluation_), Error::kSnapshot);
    if (tree_.hole) Require(tree_.hole->value < tree_.symbols.size(), Error::kSnapshot);
    const auto& block = sequences_.Block(tree_.body);
    std::uint32_t count = 0;
    while (count < block.child_count) {
      budget_.Step();
      const auto id = sequences_.Child(block.first_child + count);
      const auto* declaration = std::get_if<DeclarationStmt>(&tree_.statements[id.value].statement);
      if (!declaration) break;
      const auto& symbol = tree_.symbols[declaration->symbol.value];
      if (symbol.kind == BindingKind::kLocal) break;
      if (tree_.hole == declaration->symbol) {
        Require(!flow_.hole_initializer && declaration->initializer && symbol.scope == tree_.body_scope &&
            symbol.kind == BindingKind::kCompilerTemporary && symbol.declaration == DeclarationKind::kConst &&
            symbol.temporary == TemporaryReason::kInternalSentinel &&
            std::holds_alternative<EmptyObjectExpr>(tree_.expressions[declaration->initializer->value].expression), Error::kSnapshot);
        const auto origin = tree_.expressions[declaration->initializer->value].origin;
        const auto* binding = std::get_if<BindingOrigin>(&tree_.origins[origin.value]);
        Require(binding && binding->symbol == declaration->symbol && tree_.statements[id.value].origin == origin,
            Error::kSnapshot);
        declared_[declaration->symbol.value] = assigned_[declaration->symbol.value] = 1;
        flow_.hole_initializer = declaration->initializer;
        ++count; continue;
      }
      Require(symbol.kind == BindingKind::kCompilerTemporary && symbol.scope == tree_.body_scope &&
              symbol.declaration == DeclarationKind::kLet && symbol.temporary &&
              !declaration->initializer && !declared_[declaration->symbol.value], Error::kSnapshot);
      declared_[declaration->symbol.value] = 1;
      ++count;
    }
    Require(!tree_.hole || flow_.hole_initializer.has_value(), Error::kSnapshot);
    // Locals are consumed only at their checked environment allocations.
    for (const auto& symbol : tree_.symbols) {
      budget_.Step();
      Require(
          symbol.kind == BindingKind::kParameter ||
              symbol.kind == BindingKind::kCompilerTemporary ||
              symbol.kind == BindingKind::kLocal ||
              symbol.kind == BindingKind::kCapture ||
              symbol.kind == BindingKind::kModule ||
              symbol.kind == BindingKind::kFunctionSelf ||
              (tree_.class_scope && symbol.kind == BindingKind::kClassInner) ||
              (exceptions_ && symbol.kind == BindingKind::kCatchParameter),
          Error::kSnapshot);
    }
    return count;
  }
  void NumericUpdates() {
    bool any = false;
    for (const auto& node : tree_.expressions) {
      budget_.Step(); any |= std::holds_alternative<UpdateExpr>(node.expression);
    }
    if (!any) return;
    std::vector<std::optional<ExprId>> saves, updates, publications;
    std::vector<std::uint8_t> published;
    budget_.Array(saves, tree_.symbols.size());
    budget_.Array(updates, tree_.symbols.size());
    budget_.Array(publications, tree_.symbols.size());
    budget_.Array(published, tree_.symbols.size());
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      budget_.Step();
      const auto& op = graph_.ops[i];
      const auto* unary = std::get_if<analysis::ValueUnary>(&op.operation);
      if (op.life != analysis::ValueLife::kLive || evaluation_.operations[i].kind == EvaluationKind::kElidedPure ||
          !unary || MemberUpdateIntrinsic(evaluation_, {static_cast<std::uint32_t>(i)}) || !NeedsNumericScratch(graph_, op)) continue;
      if (unary->op == analysis::UnaryOperator::kToNumeric)
        Require(ResourceFailures(tree_.identity.contract) == ResourceFailureScope::kExcluded, Error::kSnapshot);
      Require(flow_.expressions[i].has_value(), Error::kSnapshot);
      const auto* sequence = std::get_if<SequenceExpr>(&tree_.expressions[flow_.expressions[i]->value].expression);
      Require(sequence, Error::kSnapshot);
      const auto* save = std::get_if<AssignmentExpr>(&tree_.expressions[sequence->left.value].expression);
      const auto* update = std::get_if<UpdateExpr>(&tree_.expressions[sequence->right.value].expression);
      const auto* target = save ? std::get_if<SymbolTarget>(&save->target) : nullptr;
      Require(target && update && target->symbol == update->symbol, Error::kSnapshot);
      const auto symbol = target->symbol;
      const auto& binding = tree_.symbols[symbol.value];
      const bool result_owned = flow_.snapshots[i] == symbol;
      Require(binding.kind == BindingKind::kCompilerTemporary && binding.scope == tree_.body_scope &&
          binding.declaration == DeclarationKind::kLet && declared_[symbol.value] &&
          !updates[symbol.value], Error::kSnapshot);
      if (result_owned) {
        // RootExpression reconstructs the actual publication and producer
        // identity. Original operand/availability checks forbid pre-Commit
        // reads. No partial conversion state can become a lexical capture.
        Require(assigned_[symbol.value] && binding.temporary == evaluation_.operations[i].temporary,
            Error::kSnapshot);
        publications[symbol.value] = flow_.expressions[i];
      } else {
        Require(binding.temporary == TemporaryReason::kNumericUpdate && !assigned_[symbol.value], Error::kSnapshot);
        assigned_[symbol.value] = 1;
      }
      saves[symbol.value] = sequence->left; updates[symbol.value] = sequence->right;
    }
    // Check actual writes/captures, independently of allocation decisions.
    // Shared storage permits only this operation's save/update/publication;
    // its later SSA reads remain subject to the common operand proof.
    const auto numeric_symbol = [&](SymbolId symbol) {
      return updates[symbol.value].has_value() ||
          tree_.symbols[symbol.value].temporary == TemporaryReason::kNumericUpdate;
    };
    for (std::size_t i = 0; i < tree_.expressions.size(); ++i) {
      budget_.Step(); const auto id = ExprId{static_cast<std::uint32_t>(i)};
      const auto& expression = tree_.expressions[i].expression;
      if (const auto* read = std::get_if<SymbolExpr>(&expression))
        Require(!numeric_symbol(read->symbol) || publications[read->symbol.value], Error::kSnapshot);
      if (const auto* save = std::get_if<AssignmentExpr>(&expression))
        if (const auto* target = std::get_if<SymbolTarget>(&save->target); target && numeric_symbol(target->symbol)) {
          if (saves[target->symbol.value] != id) {
            Require(publications[target->symbol.value] == save->value &&
                !published[target->symbol.value], Error::kSnapshot);
            published[target->symbol.value] = 1;
          }
        }
      if (const auto* update = std::get_if<UpdateExpr>(&expression))
        Require(numeric_symbol(update->symbol) && updates[update->symbol.value] == id, Error::kSnapshot);
    }
    for (std::size_t i = 0; i < publications.size(); ++i) {
      budget_.Step(); Require(!publications[i] || published[i], Error::kSnapshot);
    }
    for (const auto symbol : tree_.closure_captures) {
      budget_.Step(); Require(!numeric_symbol(symbol), Error::kSnapshot);
    }
  }
  void PredicateSnapshots() {
    bool any = false;
    for (const auto& op : evaluation_.operations) { budget_.Step(); any |= op.kind == EvaluationKind::kPredicateSnapshot; }
    if (!any) return;
    std::vector<std::optional<analysis::OpId>> sources;
    budget_.Array(sources, tree_.expressions.size());
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      budget_.Step(); if (evaluation_.operations[i].kind != EvaluationKind::kPredicateSnapshot) continue;
      Require(flow_.expressions[i].has_value(), Error::kSnapshot);
      auto& source = sources[flow_.expressions[i]->value]; Require(!source, Error::kSnapshot);
      source = analysis::OpId{static_cast<std::uint32_t>(i)};
      if (NativeBinding(*flow_.expressions[i]) ||
          ReceiverRoot(*source, *flow_.expressions[i]) != ReceiverValue::kNone)
        RootExpression(*source, *flow_.expressions[i]);
    }
    for (std::size_t i = 0; i < tree_.expressions.size(); ++i) {
      budget_.Step(); const auto& expression = tree_.expressions[i];
      const auto* assignment = std::get_if<AssignmentExpr>(&expression.expression);
      if (!assignment || !sources[assignment->value.value]) continue;
      const auto source = *sources[assignment->value.value];
      Require(expression.origin.value == graph_.ops[source.value].origin.value, Error::kSnapshot);
      RootExpression(source, ExprId{static_cast<std::uint32_t>(i)});
    }
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      budget_.Step(); if (evaluation_.operations[i].kind == EvaluationKind::kPredicateSnapshot)
        Require(flow_.snapshots[i].has_value() || flow_.receivers[i] != ReceiverValue::kNone, Error::kSnapshot);
    }
  }
  ReceiverValue ReceiverRoot(analysis::OpId id, ExprId expression) const {
    const auto shape = DescribeEvaluation(graph_.ops[id.value].operation);
    if (!shape || shape->receiver == ReceiverValue::kNone) return ReceiverValue::kNone;
    const auto& node = tree_.expressions[expression.value].expression;
    switch (shape->receiver) {
      case ReceiverValue::kCurrent:
        if (const auto* read = std::get_if<ThisExpr>(&node); read && read->owner == tree_.owner)
          return ReceiverValue::kCurrent;
        break;
      case ReceiverValue::kInitialized:
        if (tree_.callable == CallableKind::kDerivedConstructor && std::holds_alternative<SuperCallExpr>(node))
          return ReceiverValue::kInitialized;
        break;
      case ReceiverValue::kNone: break;
    }
    return ReceiverValue::kNone;
  }
  std::optional<SymbolId> NativeBinding(ExprId expression) const {
    const auto* read = std::get_if<SymbolExpr>(&tree_.expressions[expression.value].expression);
    if (!read) return std::nullopt;
    const auto& symbol = tree_.symbols[read->symbol.value];
    if (symbol.kind != BindingKind::kParameter && symbol.kind != BindingKind::kFunctionSelf)
      return std::nullopt;
    Require(!written_[read->symbol.value], Error::kSnapshot);
    return read->symbol;
  }
  void RootExpression(analysis::OpId id, ExprId expression) {
    Require(flow_.expressions[id.value].has_value(), Error::kPlacement);
    const auto value = *flow_.expressions[id.value];
    const auto& evaluation = evaluation_.operations[id.value];
    if (!evaluation.temporary) {
      Require(expression == value, Error::kPlacement);
      return;
    }
    if (expression == value) {
      if (const auto receiver = ReceiverRoot(id, expression); receiver != ReceiverValue::kNone) {
        Require(tree_.expressions[expression.value].origin.value == graph_.ops[id.value].origin.value,
            Error::kSnapshot);
        flow_.receivers[id.value] = receiver;
        return;
      }
      if (const auto binding = NativeBinding(expression)) {
        Require(tree_.expressions[expression.value].origin.value == graph_.ops[id.value].origin.value,
            Error::kSnapshot);
        flow_.snapshots[id.value] = *binding;
        return;
      }
    }
    const auto* assignment = std::get_if<AssignmentExpr>(&tree_.expressions[expression.value].expression);
    Require(assignment && assignment->value == value &&
        tree_.expressions[expression.value].origin.value == graph_.ops[id.value].origin.value, Error::kSnapshot);
    const auto* target = std::get_if<SymbolTarget>(&assignment->target);
    Require(target != nullptr, Error::kSnapshot);
    const auto& symbol = tree_.symbols[target->symbol.value];
    Require(symbol.kind == BindingKind::kCompilerTemporary && symbol.scope == tree_.body_scope &&
            symbol.declaration == DeclarationKind::kLet && symbol.temporary == evaluation.temporary &&
            declared_[target->symbol.value] && !assigned_[target->symbol.value], Error::kSnapshot);
    assigned_[target->symbol.value] = 1;
    flow_.snapshots[id.value] = target->symbol;
    if(graph_.ops[id.value].result)
      flow_.definitions[expression.value]=OriginalSourceDefinition{
          OriginalResultDefinition{id,*graph_.ops[id.value].result},target->symbol,value};
  }
  void Arm(std::optional<ControlId> first, StmtId block, TreeOriginId origin,
      analysis::EdgeId edge, std::size_t& pending, bool copies = true) {
    Require(tree_.statements[block.value].origin == origin, Error::kPlacement);
    const auto& syntax = sequences_.Block(block);
    std::uint32_t child = 0;
    if (copies) edges_->Consume(edge, syntax, child);
    if (!first) { Require(child == syntax.child_count, Error::kSequence); return; }
    Require(pending < tasks_.size(), Error::kResourceLimit);
    tasks_[pending++] = {*first, block, child};
  }

  const analysis::ValueProgram& graph_;
  const ControlPlan& control_;
  const EvaluationPlan& evaluation_;
  const FunctionTree& tree_;
  TreeCheckBudget& budget_;
  const ExceptionTreeInput* exceptions_;
  std::optional<ForOfEmissionView> iteration_;
  std::vector<std::optional<StmtId>> iteration_statements_;
  std::vector<std::optional<ScopeId>> iteration_scopes_;
  std::optional<ForInEmissionView> enumeration_;
  std::vector<std::optional<StmtId>> enumeration_statements_;
  std::vector<std::optional<ScopeId>> enumeration_scopes_;
  // Per deferred cell: original allocation block. Normal deferral keeps it;
  // phased narrowing additionally checks the independently derived scope.
  std::vector<std::optional<StmtId>> deferred_blocks_;
  std::optional<CellLifetimes> deferred_scopes_;
  std::vector<std::optional<StmtId>> deferred_scope_blocks_;
  const CompletionTreeView& sequences_;
  std::span<const ScopeId> expression_scopes_;
  TreeFlow flow_;
  struct ConditionalTask { std::size_t region = 0; ExprId expression; std::optional<SymbolId> target; };
  std::vector<ConditionalTask> conditional_tasks_;
  std::vector<SelectionRegion> logicals_;
  std::vector<std::optional<std::size_t>> logical_branches_;
  std::vector<std::uint8_t> original_prefixes_;
  std::vector<std::uint8_t> decision_branches_;
  std::vector<std::uint8_t> decision_expressions_;
  std::vector<std::optional<std::size_t>> placements_;
  std::vector<std::uint8_t> assigned_;
  std::vector<std::uint8_t> declared_;
  std::vector<std::uint8_t> written_;
  std::vector<TreeRegion> tasks_;
  std::vector<std::optional<StmtId>> loops_;
  std::optional<TreeEdgeFlow> edges_;
};

std::variant<TreeFlow, TreeFlowError> VerifyFlow(
    const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    analysis::SemanticBudget budget, const ExceptionTreeInput* exceptions,
    TreeControlCheck control_check, TreeLocation* failure_location) {
  if (failure_location) *failure_location = {};
  try {
    // Partial syntax is printable, but cannot certify correspondence to a whole
    // function. Keep the old semantic route from interpreting a gap as a no-op.
    Require(tree.recovery_issues.empty(), Error::kStructure);
    Require(tree.value_revision == graph.revision && tree.cfg_revision == graph.cfg_revision, Error::kRevision);
    const auto structure = VerifyTreeStructure(tree, budget);
    if (const auto* error = std::get_if<TreeStructureError>(&structure)) {
      if (*error == TreeStructureError::kResourceLimit) return Error::kResourceLimit;
      if (*error == TreeStructureError::kWorkLimit) return Error::kWorkLimit;
      return Error::kStructure;
    }
    TreeCheckBudget checked(budget);
    // Keep completed verifier scratch conservatively charged; it is not an
    // excuse to give each independent check a fresh method-wide work allowance.
    checked.Admit(std::get<TreeStructure>(structure).allocated_bytes, 1);
    checked.Step(std::get<TreeStructure>(structure).work);
    if (exceptions && control_check == TreeControlCheck::kFresh) {
      const auto regions = VerifyExceptionControl(exceptions->body, graph, exceptions->input,
          exceptions->regions, exceptions->control, checked.Remaining(), exceptions->for_of?&exceptions->for_of->ownership:nullptr);
      if (const auto* error = std::get_if<ExceptionControlError>(&regions)) {
        if (*error == ExceptionControlError::kResourceLimit) return Error::kResourceLimit;
        if (*error == ExceptionControlError::kWorkLimit) return Error::kWorkLimit;
        return Error::kControl;
      }
      checked.Step(std::get<std::uint64_t>(regions));
    } else if (!exceptions) {
      const auto regions = VerifyControlPlan(graph, control, checked.Remaining());
      if (const auto* error = std::get_if<ControlError>(&regions)) {
        if (*error == ControlError::kResourceLimit) return Error::kResourceLimit;
        if (*error == ControlError::kWorkLimit) return Error::kWorkLimit;
        return Error::kControl;
      }
      checked.Step(std::get<std::uint64_t>(regions));
    }
    const auto effects = exceptions ? VerifyPhasedEvaluationPlan(graph,evaluation,checked.Remaining(),exceptions->for_of?&*exceptions->for_of:nullptr) : VerifyEvaluationPlan(graph,evaluation,checked.Remaining());
    if (const auto* error = std::get_if<EvaluationFailure>(&effects)) {
      if (*error == EvaluationError::kResourceLimit) return Error::kResourceLimit;
      if (*error == EvaluationError::kWorkLimit) return Error::kWorkLimit;
      return Error::kEvaluation;
    }
    checked.Step(std::get<std::uint64_t>(effects));
    checked.Admit(1, sizeof(FlowCheck));
    CompletionPlan completions;
    if (!tree.finalizers.empty() || !tree.yield_returns.empty()) {
      Require(exceptions != nullptr, Error::kControl);
      auto result = BuildCompletionPlan(graph, exceptions->regions, exceptions->control,
          evaluation, checked.Remaining());
      if (const auto* error = std::get_if<CompletionError>(&result)) {
        if (*error == CompletionError::kWorkLimit) return Error::kWorkLimit;
        if (*error == CompletionError::kResourceLimit) return Error::kResourceLimit;
        return Error::kControl;
      }
      completions = std::get<CompletionPlan>(std::move(result));
    }
    const CompletionTreeView sequences(tree, completions, checked);
    return FlowCheck(graph, control, evaluation, tree, checked, sequences,
        std::get<TreeStructure>(structure).expression_scopes, exceptions).Run();
  } catch (const FlowFailure& failure) {
    if (failure_location) *failure_location = failure.location;
    return failure.kind;
  } catch (Error error) { return error;
  } catch (EvaluationError error) {
    if (error == EvaluationError::kWorkLimit) return Error::kWorkLimit;
    if (error == EvaluationError::kResourceLimit) return Error::kResourceLimit;
    return Error::kEvaluation;
  } catch (TreeStructureError error) {
    return error == TreeStructureError::kWorkLimit ? Error::kWorkLimit : Error::kResourceLimit;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace
std::variant<TreeFlow, TreeFlowError> VerifyTreeFlow(
    const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    analysis::SemanticBudget budget, TreeLocation* failure_location) {
  return VerifyFlow(graph, control, evaluation, tree, budget, nullptr, TreeControlCheck::kFresh, failure_location);
}
std::variant<TreeFlow, TreeFlowError> VerifyExceptionTreeFlow(
    const analysis::ValueProgram& graph, const ExceptionTreeInput& exceptions,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    analysis::SemanticBudget budget, TreeControlCheck control_check, TreeLocation* failure_location) {
  return VerifyFlow(graph, exceptions.control.skeleton, evaluation, tree, budget, &exceptions, control_check, failure_location);
}
}  // namespace abcd::source
