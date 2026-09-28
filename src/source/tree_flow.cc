#include "tree_flow.h"

#include <algorithm>
#include <new>
#include <stdexcept>

#include "completion_tree.h"
#include "exception_tree.h"
#include "tree_checks.h"
#include "tree_edge_flow.h"

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
    budget.Array(flow_.controls, control.nodes.size() -
        (exceptions ? exceptions->control.units.size() : graph.blocks.size()));
    flow_.first_control = {static_cast<std::uint32_t>(control.nodes.size() - flow_.controls.size())};
    budget.Array(flow_.control_statements, flow_.controls.size());
    edges_.emplace(graph, evaluation, tree, flow_, declared_, assigned_, budget, sequences);
    for (const auto symbol : sequences.Symbols()) {
      Require(!declared_[symbol.value], Error::kPlacement);
      declared_[symbol.value] = assigned_[symbol.value] = 1;
    }
  }
  TreeFlow Run() {
    IndexPlacements();
    ModulePairs();
    CheckBodyOrigin();
    CapturedCells();
    ModuleAliases();
    auto first_child = Declarations();
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
              evaluation_.operations[id.value].kind == EvaluationKind::kInitializer ||
              evaluation_.operations[id.value].kind == EvaluationKind::kModuleGuard) continue;
          const auto& placement = tree_.operations[*placements_[id.value]].node;
          if (std::holds_alternative<analysis::ValueJump>(op.operation) || IsEnvironmentState(op.operation) ||
              evaluation_.operations[id.value].kind == EvaluationKind::kElidedPure ||
              evaluation_.operations[id.value].kind == EvaluationKind::kAlias) {
            const auto* anchor = std::get_if<AnchorId>(&placement);
            Require(anchor != nullptr, Error::kPlacement);
            const auto* boundary = std::get_if<BlockBoundary>(&sequences_.Anchor(*anchor));
            Require(boundary && boundary->block == region.block && boundary->index == child, Error::kSequence);
            if (IsEnvironmentState(op.operation)) CellDeclarations(id, block, child);
            continue;
          }
          Require(child < block.child_count, Error::kSequence);
          const auto statement = sequences_.Child(block.first_child + child++);
          flow_.statements[id.value] = statement;
          const auto& syntax = tree_.statements[statement.value].statement;
          if (std::holds_alternative<analysis::ValueBranch>(op.operation)) {
            Require(std::get_if<StmtId>(&placement) && std::get<StmtId>(placement) == statement, Error::kPlacement);
            const auto* branch = std::get_if<IfStmt>(&syntax);
            Require(branch && node.arms, Error::kControl);
            flow_.expressions[id.value] = branch->condition;
            const TreeOriginId origin{op.origin.value};
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
          tree_.symbols[alias.symbol.value].kind == BindingKind::kModule &&
          tree_.symbols[alias.symbol.value].scope == tree_.module_scope, Error::kPlacement);
      declared_[alias.symbol.value] = 1;
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
    std::vector<std::uint32_t> definitions;
    budget_.Array(definitions, graph_.ops.size());
    if (evaluation_.literals) for (const auto& object : evaluation_.literals->objects) {
      budget_.Step();
      const auto expression = flow_.expressions[object.allocation.value];
      Require(expression.has_value(), Error::kPlacement);
      const auto* syntax = std::get_if<ObjectExpr>(&tree_.expressions[expression->value].expression);
      Require(syntax && syntax->properties.count >= object.count, Error::kPlacement);
      const auto prefix = syntax->properties.count - object.count;
      const auto& allocation = std::get<analysis::ValueObjectAllocation>(graph_.ops[object.allocation.value].operation);
      Require(allocation.literal || prefix == 0, Error::kPlacement);
      definitions[object.allocation.value] = object.count;
      for (std::uint32_t i = 0; i < object.count; ++i) {
        budget_.Step(); ++count;
        const auto id = evaluation_.literals->definitions[object.first + i];
        Require(flow_.initializers[id.value] == PropertyId{syntax->properties.first + prefix + i}, Error::kPlacement);
        const auto& property = tree_.properties[syntax->properties.first + prefix + i];
        Require(property.origin.value == graph_.ops[id.value].origin.value, Error::kPlacement);
      }
    }
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      budget_.Step(); const auto& op = graph_.ops[i];
      const auto* allocation = std::get_if<analysis::ValueObjectAllocation>(&op.operation);
      if (op.life != analysis::ValueLife::kLive || !allocation || !allocation->literal) continue;
      Require(flow_.expressions[i].has_value(), Error::kPlacement);
      const auto* object = std::get_if<ObjectExpr>(&tree_.expressions[flow_.expressions[i]->value].expression);
      Require(object != nullptr, Error::kPlacement);
      Require(definitions[i] <= object->properties.count, Error::kPlacement);
      const auto prefix = object->properties.count - definitions[i];
      flow_.buffered_properties[i] = prefix;
      for (std::uint32_t p = 0; p < prefix; ++p) {
        budget_.Step(); ++count;
        const auto& property = tree_.properties[object->properties.first + p];
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
      if (std::holds_alternative<analysis::ValueOwnWrite>(op.operation)) {
        const auto* property = std::get_if<PropertyId>(&placement);
        Require(property != nullptr, Error::kPlacement);
        flow_.initializers[i] = *property;
        flow_.expressions[i] = tree_.properties[property->value].value;
      } else if (std::holds_alternative<analysis::ValueBranch>(op.operation) ||
          std::holds_alternative<analysis::ValueDebugger>(op.operation) ||
          std::holds_alternative<analysis::ValueThrow>(op.operation) ||
          std::holds_alternative<analysis::ValueReturn>(op.operation)) {
        Require(std::holds_alternative<StmtId>(placement), Error::kPlacement);
      } else if (std::holds_alternative<analysis::ValueJump>(op.operation) || IsEnvironmentState(op.operation) ||
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
    Require(tree_.hole.has_value() == evaluation_.holes.has_value(), Error::kSnapshot);
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
    analysis::SemanticBudget budget, const ExceptionTreeInput* exceptions) {
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
    if (exceptions) {
      const auto regions = VerifyExceptionControl(exceptions->body, graph, exceptions->input,
          exceptions->regions, exceptions->control, checked.Remaining());
      if (const auto* error = std::get_if<ExceptionControlError>(&regions)) {
        if (*error == ExceptionControlError::kResourceLimit) return Error::kResourceLimit;
        if (*error == ExceptionControlError::kWorkLimit) return Error::kWorkLimit;
        return Error::kControl;
      }
      checked.Step(std::get<std::uint64_t>(regions));
    } else {
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
    if (!tree.finalizers.empty()) {
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
    analysis::SemanticBudget budget) {
  return VerifyFlow(graph, exceptions.control.skeleton, evaluation, tree, budget, &exceptions);
}
}  // namespace abcd::source
