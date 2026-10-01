#include "tree_flow.h"

#include <algorithm>
#include <new>
#include <stdexcept>

#include "completion_tree.h"
#include "exception_tree.h"
#include "tree_checks.h"
#include "tree_edge_flow.h"
#include "logical_regions.h"

namespace abcd::source {
namespace {
using Error = TreeFlowError;
void Require(bool condition, Error error) { if (!condition) throw error; }
struct TreeRegion {
  ControlId entry;
  StmtId block;
  std::uint32_t first_child = 0;
};
class FlowCheck {
 public:
  FlowCheck(const analysis::ValueProgram& graph, const ControlPlan& control,
      const EvaluationPlan& evaluation, const FunctionTree& tree,
      TreeCheckBudget& budget, const CompletionTreeView& sequences,
      const ExceptionTreeInput* exceptions = nullptr)
      : graph_(graph), control_(control), evaluation_(evaluation), tree_(tree), budget_(budget),
        exceptions_(exceptions), sequences_(sequences) {
    budget.Array(placements_, graph.ops.size());
    budget.Array(flow_.expressions, graph.ops.size());
    budget.Array(flow_.statements, graph.ops.size());
    budget.Array(flow_.initializers, graph.ops.size());
    budget.Array(flow_.buffered_properties, graph.ops.size());
    budget.Array(flow_.defined_properties, tree.properties.size());
    budget.Array(flow_.snapshots, graph.ops.size());
    if (evaluation.cells) budget.Array(flow_.cells, evaluation.cells->cells.cells.size());
    budget.Array(assigned_, tree.symbols.size());
    budget.Array(declared_, tree.symbols.size());
    budget.Array(tasks_, control.nodes.size());
    budget.Array(loops_, control.loops.size());
    if (exceptions) {
      budget.Array(flow_.catch_parameters, graph.values.size());
      budget.Array(flow_.tries, exceptions->regions.regions.size());
    }
    budget.Array(flow_.branch_polarity, graph.ops.size());
    budget.Array(flow_.controls, control.nodes.size() -
        (exceptions ? exceptions->control.units.size() : graph.blocks.size()));
    flow_.first_control = {static_cast<std::uint32_t>(control.nodes.size() - flow_.controls.size())};
    budget.Array(flow_.control_statements, flow_.controls.size());
    if (!exceptions && evaluation.phis) {
      logicals_ = FindSelectionExpressions(graph, control, evaluation, budget);
      if (!logicals_.empty()) {
        budget.Array(logical_branches_, graph.ops.size());
        budget.Array(flow_.logical_operations, graph.ops.size());
        budget.Array(flow_.conditional_operations, graph.ops.size());
        budget.Array(conditional_tasks_, logicals_.size());
        for (std::size_t i = 0; i < logicals_.size(); ++i) logical_branches_[logicals_[i].branch.value] = i;
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
          if (const auto* region = std::get_if<TryControlUnit>(&unit)) {
            Try(region->region, block, child, pending);
            cursor = node.next;
            continue;
          }
          if (std::holds_alternative<TryExitUnit>(unit)) {
            cursor = node.next;
            continue;
          }
          const auto original = std::get<PhaseControlUnit>(unit).block;
          Require(node.block == original, Error::kControl);
          if (graph_.phases[original.value].phase == analysis::ExceptionPhase::kCatch)
            edges_->Catch(original, block, child);
        }
        if (!std::holds_alternative<std::monostate>(node.synthetic)) {
          Loop(*cursor, block, child, pending);
          cursor = node.next;
          continue;
        }
        const auto& source = graph_.blocks[node.block.value];
        // Branch operations live at Before, but their successors live at Commit.
        // Only a checked empty boundary permits the predicate at Commit; no
        // normal-copy writes or effects may intervene in that relocation.
        std::optional<analysis::OpId> predicate;
        if (exceptions_) {
          const auto& phase = graph_.phases[node.block.value];
          if (phase.phase == analysis::ExceptionPhase::kCommit && phase.operation &&
              std::holds_alternative<analysis::ValueBranch>(graph_.ops[phase.operation->value].operation))
            predicate = phase.operation;
        }
        const auto count = predicate ? 1U : source.op_count;
        for (std::uint32_t i = 0; i < count; ++i) {
          budget_.Step();
          const analysis::OpId id = predicate.value_or(analysis::OpId{source.first_op.value + i});
          const auto& op = graph_.ops[id.value];
          if (exceptions_ && !predicate && std::holds_alternative<analysis::ValueBranch>(op.operation)) {
            Require(evaluation_.operations[id.value].kind == EvaluationKind::kRoot &&
                source.edge_count == 1 && !edges_->HasCopies(source.first_edge), Error::kControl);
            continue;
          }
          if (exceptions_ && op.life == analysis::ValueLife::kLive)
            edges_->Stage(id, block, child);
          if (op.life != analysis::ValueLife::kLive ||
              evaluation_.operations[id.value].kind == EvaluationKind::kInline ||
              evaluation_.operations[id.value].kind == EvaluationKind::kPredicateSnapshot ||
              evaluation_.operations[id.value].kind == EvaluationKind::kPhiInput ||
              evaluation_.operations[id.value].kind == EvaluationKind::kLogicalLeft ||
              evaluation_.operations[id.value].kind == EvaluationKind::kNestedLiteral ||
              evaluation_.operations[id.value].kind == EvaluationKind::kInitializer ||
              evaluation_.operations[id.value].kind == EvaluationKind::kClassProjection ||
              evaluation_.operations[id.value].kind == EvaluationKind::kModuleGuard) continue;
          const auto& placement = tree_.operations[*placements_[id.value]].node;
          if (IsConstructorGuard(tree_, id) || evaluation_.operations[id.value].kind == EvaluationKind::kCellGuard || IsAbsentHeritageTransport(graph_, evaluation_, op) ||
              std::holds_alternative<analysis::ValueJump>(op.operation) || IsEnvironmentState(op.operation) ||
              evaluation_.operations[id.value].kind == EvaluationKind::kElidedPure ||
              evaluation_.operations[id.value].kind == EvaluationKind::kAlias) {
            const auto* anchor = std::get_if<AnchorId>(&placement);
            Require(anchor != nullptr, Error::kPlacement);
            const auto* boundary = std::get_if<BlockBoundary>(&sequences_.Anchor(*anchor));
            Require(boundary && boundary->block == region.block && boundary->index == child, Error::kSequence);
            if (IsEnvironmentState(op.operation)) CellDeclarations(id, block, child);
            continue;
          }
          if (evaluation_.operations[id.value].kind == EvaluationKind::kLogicalValue) {
            Require(!logical_branches_.empty() && logical_branches_[id.value], Error::kPlacement);
            LogicalValue(logicals_[*logical_branches_[id.value]], region.block, child);
            continue;
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
            const auto* branch = std::get_if<IfStmt>(&syntax);
            Require(branch && node.arms, Error::kControl);
            flow_.expressions[id.value] = branch->condition;
            const TreeOriginId origin{op.origin.value};
            if (!exceptions_ && !node.arms->taken && node.arms->fallthrough && !branch->alternate) {
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
            RootExpression(id, expression->expression);
          }
        }
        if (!node.arms) {
          std::uint8_t normal = 0;
          for (std::uint8_t e = 0; e < source.edge_count; ++e) {
            budget_.Step(); const analysis::EdgeId edge{source.first_edge.value + e};
            if (exceptions_ && graph_.edges[edge.value].kind == analysis::FlowEdgeKind::kException) continue;
            Require(++normal == 1, Error::kControl);
            edges_->Consume(edge, block, child);
          }
        }
        cursor = node.next;
      }
      Require(child == block.child_count, Error::kSequence);
    }
    edges_->Finish();
    Objects();
    for (std::size_t i = 0; i < tree_.symbols.size(); ++i) {
      budget_.Step();
      const auto& symbol = tree_.symbols[i];
      if (symbol.kind == BindingKind::kParameter || symbol.kind == BindingKind::kFunctionSelf) {
        Require(!declared_[i] && !assigned_[i], Error::kSnapshot);
      } else if (symbol.kind == BindingKind::kLocal || symbol.kind == BindingKind::kCapture ||
                 symbol.kind == BindingKind::kModule || symbol.kind == BindingKind::kCatchParameter) {
        Require(declared_[i] && !assigned_[i], Error::kSnapshot);
      } else Require(declared_[i] && assigned_[i], Error::kSnapshot);
    }
    for (const auto cell : flow_.cells) { budget_.Step(); Require(cell.has_value(), Error::kPlacement); }
    if (exceptions_) {
      for (std::size_t i = 0; i < graph_.values.size(); ++i) {
        budget_.Step();
        const auto& value = graph_.values[i];
        Require(flow_.catch_parameters[i].has_value() ==
            (value.life == analysis::ValueLife::kLive &&
             std::holds_alternative<analysis::CatchDefinition>(value.origin)), Error::kPlacement);
      }
      for (const auto statement : flow_.tries) {
        budget_.Step(); Require(statement.has_value(), Error::kControl);
      }
    }
    sequences_.Finish(flow_, budget_);
    flow_.value_revision = graph_.revision;
    flow_.cfg_revision = graph_.cfg_revision;
    flow_.allocated_bytes = budget_.Bytes();
    flow_.work = budget_.Work();
    return std::move(flow_);
  }

 private:
  void LogicalBranch(const SelectionRegion& region, StmtId statement, StmtId parent, std::uint32_t child) {
    const auto root = std::get<ExpressionStmt>(tree_.statements[statement.value].statement).expression;
    const auto* assignment = std::get_if<AssignmentExpr>(&tree_.expressions[root.value].expression);
    Require(assignment != nullptr, Error::kPlacement);
    const auto* target = std::get_if<SymbolTarget>(&assignment->target);
    if (region.consequent) {
      Require(target && tree_.statements[statement.value].origin.value == graph_.ops[region.branch.value].origin.value &&
          tree_.expressions[root.value].origin == tree_.statements[statement.value].origin, Error::kPlacement);
      Conditional(region, assignment->value, target->symbol, parent, child);
      return;
    }
    const auto* logical = std::get_if<LogicalExpr>(&tree_.expressions[assignment->value.value].expression);
    Require(target && logical && logical->operation == region.operation &&
        flow_.expressions[region.boolean.value] == assignment->value &&
        tree_.statements[statement.value].origin.value == graph_.ops[region.branch.value].origin.value &&
        tree_.expressions[root.value].origin == tree_.statements[statement.value].origin, Error::kPlacement);
    const auto rhs = std::get<analysis::ValueId>(region.right);
    const auto producer = std::get<analysis::OpResult>(graph_.values[rhs.value].origin).op;
    Require(flow_.expressions[producer.value] == logical->right, Error::kPlacement);
    flow_.expressions[region.branch.value] = assignment->value;
    flow_.logical_operations[region.boolean.value] = flow_.logical_operations[region.branch.value] = region.operation;
    LogicalEdges(region, target->symbol, logical->left, logical->right, parent, child);
  }
  void Conditional(const SelectionRegion& region, ExprId expression, std::optional<SymbolId> target,
      StmtId parent, std::uint32_t child) {
    std::size_t pending = 1;
    conditional_tasks_[0] = {static_cast<std::size_t>(&region - logicals_.data()), expression};
    while (pending) {
      budget_.Step(); const auto task = conditional_tasks_[--pending]; const auto& current = logicals_[task.region];
      const auto* conditional = std::get_if<ConditionalExpr>(&tree_.expressions[task.expression.value].expression);
      Require(conditional && flow_.expressions[current.boolean.value] == conditional->condition, Error::kPlacement);
      const auto arm = [&](const SelectionOperand& input, ExprId actual) {
        if (const auto* nested = std::get_if<SelectionChild>(&input)) {
          Require(nested->branch.value < logical_branches_.size() && logical_branches_[nested->branch.value], Error::kPlacement);
          const auto index = *logical_branches_[nested->branch.value];
          Require(logicals_[index].parent == current.branch && logicals_[index].merged == current.merged &&
              flow_.expressions[nested->branch.value] == actual && pending < conditional_tasks_.size(), Error::kPlacement);
          conditional_tasks_[pending++] = {index, actual};
        } else {
          const auto producer = std::get<analysis::OpResult>(graph_.values[std::get<analysis::ValueId>(input).value].origin).op;
          Require(flow_.expressions[producer.value] == actual, Error::kPlacement);
        }
      };
      Require(current.consequent.has_value(), Error::kPlacement);
      arm(*current.consequent, conditional->consequent); arm(current.right, conditional->alternate);
      flow_.expressions[current.branch.value] = task.expression;
      flow_.conditional_operations[current.branch.value] = 1;
      LogicalEdges(current, target, conditional->consequent, conditional->alternate, parent, child);
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
    LogicalEdges(region, {}, logical->left, logical->right, parent, child);
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
        anchors(part.block); path = part.next;
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
    const auto& planned = exceptions_->control.regions[id.value];
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
    for (const auto& module : evaluation_.modules) {
      budget_.Step();
      const auto read = module.pair.read.value;
      const auto guard = module.pair.guard.value;
      // The evaluation checker proved exact guarded adjacency and effect order.
      // Two PC placements own ONE expression occurrence, not two executions.
      Require(flow_.expressions[read] && flow_.expressions[guard] == flow_.expressions[read], Error::kPlacement);
    }
  }
  void CapturedCells() {
    std::size_t next = 0;
    if (evaluation_.cells) {
      const auto& cells = evaluation_.cells->cells.cells;
      for (std::size_t i = 0; i < cells.size(); ++i) {
        budget_.Step();
        if (!std::holds_alternative<IncomingCell>(cells[i].storage)) continue;
        Require(evaluation_.cells->incoming == IncomingCells::kConditional && next < tree_.captures.size(), Error::kPlacement);
        const auto symbol = tree_.captures[next++].symbol;
        Require(symbol.value < tree_.symbols.size(), Error::kPlacement);
        const auto& binding = tree_.symbols[symbol.value];
        Require(binding.kind == BindingKind::kCapture && tree_.capture_scope == binding.scope &&
            binding.declaration == DeclarationKind::kLet && !binding.temporary && !declared_[symbol.value], Error::kPlacement);
        declared_[symbol.value] = 1;
        flow_.cells[i] = symbol;
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
  void CellDeclarations(analysis::OpId allocation, const BlockStmt& block, std::uint32_t& child) {
    Require(evaluation_.cells.has_value(), Error::kEvaluation);
    if (!std::holds_alternative<analysis::ValueEnvironmentPush>(graph_.ops[allocation.value].operation)) return;
    const auto& cells = evaluation_.cells->cells.cells;
    auto cell = std::lower_bound(cells.begin(), cells.end(), allocation,
        [&](const Cell& item, analysis::OpId site) {
          budget_.Step();
          const auto* local = std::get_if<AllocatedCell>(&item.storage);
          return local && local->allocation < site;
        });
    for (; cell != cells.end(); ++cell) {
      budget_.Step();
      const auto* local = std::get_if<AllocatedCell>(&cell->storage);
      if (!local || local->allocation != allocation) break;
      Require(child < block.child_count, Error::kSequence);
      const auto& statement = tree_.statements[sequences_.Child(block.first_child + child++).value];
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
    }
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
      }
      for (std::uint32_t p = syntax->buffered; p < syntax->properties.count; ++p) {
        budget_.Step();
        Require(flow_.defined_properties[syntax->properties.first + p], Error::kPlacement);
      }
      count += syntax->properties.count;
    }
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      budget_.Step(); const auto& op = graph_.ops[i];
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
      const auto expected = exceptions_ ? ExceptionArcOrigin(*exceptions_, {edge->value}) :
          std::optional<TreeOrigin>{EdgeOrigin{*edge}};
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
          cursor = exceptions_->control.regions[region->region.value].body;
          continue;
        }
        Require(std::holds_alternative<PhaseControlUnit>(unit), Error::kPlacement);
      }
      if (const auto* loop = std::get_if<LoopControl>(&node.synthetic)) {
        cursor = control_.loops[loop->loop.value].body;
        continue;
      }
      Require(std::holds_alternative<std::monostate>(node.synthetic), Error::kPlacement);
      const auto& block = graph_.blocks[node.block.value];
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
      Require(placement.operation.value < graph_.ops.size(), Error::kPlacement);
      auto& entry = placements_[placement.operation.value];
      Require(!entry && graph_.ops[placement.operation.value].life == analysis::ValueLife::kLive, Error::kPlacement);
      entry = i;
    }
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      budget_.Step();
      const auto& op = graph_.ops[i];
      if (op.life != analysis::ValueLife::kLive) continue;
      Require(placements_[i].has_value(), Error::kPlacement);
      const auto& placement = tree_.operations[*placements_[i]].node;
      // Class member installations retain a PC anchor, not an object-literal
      // PropertyId. Their class owner/body correspondence is checked separately.
      if (evaluation_.operations[i].kind == EvaluationKind::kCellGuard) {
        Require(evaluation_.cells && IsInitializedCellGuard(graph_, *evaluation_.cells,
            analysis::OpId{static_cast<std::uint32_t>(i)}) && std::holds_alternative<AnchorId>(placement), Error::kPlacement);
        continue;
      }
      if (IsConstructorGuard(tree_, analysis::OpId{static_cast<std::uint32_t>(i)})) {
        Require(tree_.callable == CallableKind::kDerivedConstructor &&
            std::holds_alternative<analysis::ValueSuperCheck>(op.operation) &&
            std::holds_alternative<AnchorId>(placement), Error::kPlacement);
        continue;
      }
      if (evaluation_.operations[i].kind == EvaluationKind::kClassProjection) {
        Require(std::holds_alternative<AnchorId>(placement), Error::kPlacement);
        continue;
      }
      if (!IsReceiverField(graph_, op) && (!IsStagedLiteral(evaluation_.literals ? &*evaluation_.literals : nullptr, {static_cast<std::uint32_t>(i)}) || evaluation_.operations[i].kind == EvaluationKind::kInitializer) && (std::holds_alternative<analysis::ValueOwnWrite>(op.operation) || std::holds_alternative<analysis::ValueIndexedWrite>(op.operation))) {
        const auto* property = std::get_if<PropertyId>(&placement);
        Require(property != nullptr, Error::kPlacement);
        Require(!flow_.defined_properties[property->value], Error::kPlacement);
        flow_.defined_properties[property->value] = 1;
        flow_.initializers[i] = *property;
        flow_.expressions[i] = tree_.properties[property->value].value;
      } else if (std::holds_alternative<analysis::ValueModuleWrite>(op.operation) &&
                 std::holds_alternative<StmtId>(placement)) {
        // Initializations own a declaration statement; updates own assignments.
      } else if (evaluation_.operations[i].kind == EvaluationKind::kLogicalValue ||
          (!logical_branches_.empty() && logical_branches_[i] && logicals_[*logical_branches_[i]].parent)) {
        const auto* expression = std::get_if<ExprId>(&placement);
        Require(expression != nullptr, Error::kPlacement); flow_.expressions[i] = *expression;
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
      Require(symbol.kind == BindingKind::kParameter || symbol.kind == BindingKind::kCompilerTemporary ||
              symbol.kind == BindingKind::kLocal || symbol.kind == BindingKind::kCapture ||
              symbol.kind == BindingKind::kModule || symbol.kind == BindingKind::kFunctionSelf ||
              (exceptions_ && symbol.kind == BindingKind::kCatchParameter),
              Error::kSnapshot);
    }
    return count;
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
        Require(flow_.snapshots[i].has_value(), Error::kSnapshot);
    }
  }
  void RootExpression(analysis::OpId id, ExprId expression) {
    Require(flow_.expressions[id.value].has_value(), Error::kPlacement);
    const auto value = *flow_.expressions[id.value];
    const auto& evaluation = evaluation_.operations[id.value];
    if (!evaluation.temporary) {
      Require(expression == value, Error::kPlacement);
      return;
    }
    const auto* assignment = std::get_if<AssignmentExpr>(&tree_.expressions[expression.value].expression);
    Require(assignment && assignment->value == value, Error::kSnapshot);
    const auto* target = std::get_if<SymbolTarget>(&assignment->target);
    Require(target != nullptr, Error::kSnapshot);
    const auto& symbol = tree_.symbols[target->symbol.value];
    Require(symbol.kind == BindingKind::kCompilerTemporary && symbol.scope == tree_.body_scope &&
            symbol.declaration == DeclarationKind::kLet && symbol.temporary == evaluation.temporary &&
            declared_[target->symbol.value] && !assigned_[target->symbol.value], Error::kSnapshot);
    assigned_[target->symbol.value] = 1;
    flow_.snapshots[id.value] = target->symbol;
  }
  void Arm(std::optional<ControlId> first, StmtId block, TreeOriginId origin,
      analysis::EdgeId edge, std::size_t& pending) {
    Require(tree_.statements[block.value].origin == origin, Error::kPlacement);
    const auto& syntax = sequences_.Block(block);
    std::uint32_t child = 0;
    edges_->Consume(edge, syntax, child);
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
  const CompletionTreeView& sequences_;
  TreeFlow flow_;
  struct ConditionalTask { std::size_t region = 0; ExprId expression; };
  std::vector<ConditionalTask> conditional_tasks_;
  std::vector<SelectionRegion> logicals_;
  std::vector<std::optional<std::size_t>> logical_branches_;
  std::vector<std::optional<std::size_t>> placements_;
  std::vector<std::uint8_t> assigned_;
  std::vector<std::uint8_t> declared_;
  std::vector<TreeRegion> tasks_;
  std::vector<std::optional<StmtId>> loops_;
  std::optional<TreeEdgeFlow> edges_;
};

std::variant<TreeFlow, TreeFlowError> VerifyFlow(
    const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    analysis::SemanticBudget budget, const ExceptionTreeInput* exceptions,
    TreeControlCheck control_check = TreeControlCheck::kFresh) {
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
          exceptions->regions, exceptions->control, checked.Remaining());
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
    const auto verify = exceptions ? VerifyPhasedEvaluationPlan : VerifyEvaluationPlan;
    const auto effects = verify(graph, evaluation, checked.Remaining());
    if (const auto* error = std::get_if<EvaluationError>(&effects)) {
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
    return FlowCheck(graph, control, evaluation, tree, checked, sequences, exceptions).Run();
  } catch (Error error) { return error;
  } catch (TreeStructureError error) {
    return error == TreeStructureError::kWorkLimit ? Error::kWorkLimit : Error::kResourceLimit;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace
std::variant<TreeFlow, TreeFlowError> VerifyTreeFlow(
    const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    analysis::SemanticBudget budget) {
  return VerifyFlow(graph, control, evaluation, tree, budget, nullptr);
}
std::variant<TreeFlow, TreeFlowError> VerifyExceptionTreeFlow(
    const analysis::ValueProgram& graph, const ExceptionTreeInput& exceptions,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    analysis::SemanticBudget budget, TreeControlCheck control_check) {
  return VerifyFlow(graph, exceptions.control.skeleton, evaluation, tree, budget, &exceptions, control_check);
}
}  // namespace abcd::source
