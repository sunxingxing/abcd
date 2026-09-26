#include "tree_flow.h"

#include <new>
#include <stdexcept>

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
      TreeCheckBudget& budget)
      : graph_(graph), control_(control), evaluation_(evaluation), tree_(tree), budget_(budget) {
    budget.Array(placements_, graph.ops.size());
    budget.Array(flow_.expressions, graph.ops.size());
    budget.Array(flow_.statements, graph.ops.size());
    budget.Array(flow_.snapshots, graph.ops.size());
    budget.Array(assigned_, tree.symbols.size());
    budget.Array(declared_, tree.symbols.size());
    budget.Array(tasks_, graph.blocks.size());
    edges_.emplace(graph, evaluation, tree, flow_, declared_, assigned_, budget);
  }
  TreeFlow Run() {
    IndexPlacements();
    CheckBodyOrigin();
    const auto first_child = Declarations();
    std::size_t pending = 1;
    tasks_[0] = {control_.entry, tree_.body, first_child};
    while (pending != 0) {
      budget_.Step();
      auto region = tasks_[--pending];
      const auto& block = std::get<BlockStmt>(tree_.statements[region.block.value].statement);
      auto child = region.first_child;
      std::optional<ControlId> cursor = region.entry;
      while (cursor) {
        budget_.Step();
        const auto& node = control_.nodes[cursor->value];
        const auto& source = graph_.blocks[node.block.value];
        for (std::uint32_t i = 0; i < source.op_count; ++i) {
          budget_.Step();
          const analysis::OpId id{source.first_op.value + i};
          const auto& op = graph_.ops[id.value];
          if (op.life != analysis::ValueLife::kLive ||
              evaluation_.operations[id.value].kind == EvaluationKind::kInline) continue;
          const auto& placement = tree_.operations[*placements_[id.value]].node;
          if (std::holds_alternative<analysis::ValueJump>(op.operation)) {
            const auto* anchor = std::get_if<AnchorId>(&placement);
            Require(anchor != nullptr, Error::kPlacement);
            const auto* boundary = std::get_if<BlockBoundary>(&tree_.anchors[anchor->value].position);
            Require(boundary && boundary->block == region.block && boundary->index == child, Error::kSequence);
            continue;
          }
          Require(child < block.child_count, Error::kSequence);
          const auto statement = tree_.children[block.first_child + child++];
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
          } else if (std::holds_alternative<analysis::ValueReturn>(op.operation)) {
            Require(std::get_if<StmtId>(&placement) && std::get<StmtId>(placement) == statement, Error::kPlacement);
            const auto* returned = std::get_if<ReturnStmt>(&syntax);
            Require(returned && returned->value, Error::kPlacement);
            flow_.expressions[id.value] = *returned->value;
          } else {
            const auto* expression = std::get_if<ExpressionStmt>(&syntax);
            Require(expression != nullptr, Error::kSequence);
            RootExpression(id, expression->expression);
          }
        }
        if (!node.arms && source.edge_count == 1)
          edges_->Consume(source.first_edge, block, child);
        cursor = node.next;
      }
      Require(child == block.child_count, Error::kSequence);
    }
    edges_->Finish();
    for (std::size_t i = 0; i < tree_.symbols.size(); ++i) {
      budget_.Step();
      const auto& symbol = tree_.symbols[i];
      if (symbol.kind == BindingKind::kParameter) {
        Require(!declared_[i] && !assigned_[i], Error::kSnapshot);
      } else Require(declared_[i] && assigned_[i], Error::kSnapshot);
    }
    flow_.value_revision = graph_.revision;
    flow_.cfg_revision = graph_.cfg_revision;
    flow_.allocated_bytes = budget_.Bytes();
    flow_.work = budget_.Work();
    return std::move(flow_);
  }

 private:
  void CheckBodyOrigin() {
    // Independently reconstruct the retained entry operation. A tombstone
    // prologue PC must not be presented as owning all remaining body syntax.
    std::optional<ControlId> cursor = control_.entry;
    while (cursor) {
      budget_.Step();
      const auto& node = control_.nodes[cursor->value];
      const auto& block = graph_.blocks[node.block.value];
      for (std::uint32_t i = 0; i < block.op_count; ++i) {
        budget_.Step();
        const auto& op = graph_.ops[block.first_op.value + i];
        if (op.life == analysis::ValueLife::kLive) {
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
      if (std::holds_alternative<analysis::ValueBranch>(op.operation) ||
          std::holds_alternative<analysis::ValueReturn>(op.operation)) {
        Require(std::holds_alternative<StmtId>(placement), Error::kPlacement);
      } else if (std::holds_alternative<analysis::ValueJump>(op.operation)) {
        Require(std::holds_alternative<AnchorId>(placement), Error::kPlacement);
      } else {
        const auto* expression = std::get_if<ExprId>(&placement);
        Require(expression != nullptr, Error::kPlacement);
        flow_.expressions[i] = *expression;
      }
    }
  }
  std::uint32_t Declarations() {
    const auto& block = std::get<BlockStmt>(tree_.statements[tree_.body.value].statement);
    std::uint32_t count = 0;
    while (count < block.child_count) {
      budget_.Step();
      const auto id = tree_.children[block.first_child + count];
      const auto* declaration = std::get_if<DeclarationStmt>(&tree_.statements[id.value].statement);
      if (!declaration) break;
      const auto& symbol = tree_.symbols[declaration->symbol.value];
      Require(symbol.kind == BindingKind::kCompilerTemporary && symbol.scope == tree_.body_scope &&
              symbol.declaration == DeclarationKind::kLet && symbol.temporary &&
              !declaration->initializer && !declared_[declaration->symbol.value], Error::kSnapshot);
      declared_[declaration->symbol.value] = 1;
      ++count;
    }
    // Only parameters and private snapshots/phi locations are admitted here.
    // User cells/locals require their own TDZ/initialization proof.
    for (const auto& symbol : tree_.symbols) {
      budget_.Step();
      Require(symbol.kind == BindingKind::kParameter || symbol.kind == BindingKind::kCompilerTemporary,
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
    const auto& syntax = std::get<BlockStmt>(tree_.statements[block.value].statement);
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
  TreeFlow flow_;
  std::vector<std::optional<std::size_t>> placements_;
  std::vector<std::uint8_t> assigned_;
  std::vector<std::uint8_t> declared_;
  std::vector<TreeRegion> tasks_;
  std::optional<TreeEdgeFlow> edges_;
};
}  // namespace

std::variant<TreeFlow, TreeFlowError> VerifyTreeFlow(
    const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    analysis::SemanticBudget budget) {
  try {
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
    const auto regions = VerifyControlPlan(graph, control, checked.Remaining());
    if (const auto* error = std::get_if<ControlError>(&regions)) {
      if (*error == ControlError::kResourceLimit) return Error::kResourceLimit;
      if (*error == ControlError::kWorkLimit) return Error::kWorkLimit;
      return Error::kControl;
    }
    checked.Step(std::get<std::uint64_t>(regions));
    const auto effects = VerifyEvaluationPlan(graph, evaluation, checked.Remaining());
    if (const auto* error = std::get_if<EvaluationError>(&effects)) {
      if (*error == EvaluationError::kResourceLimit) return Error::kResourceLimit;
      if (*error == EvaluationError::kWorkLimit) return Error::kWorkLimit;
      return Error::kEvaluation;
    }
    checked.Step(std::get<std::uint64_t>(effects));
    checked.Admit(1, sizeof(FlowCheck));
    return FlowCheck(graph, control, evaluation, tree, checked).Run();
  } catch (Error error) { return error;
  } catch (TreeStructureError error) {
    return error == TreeStructureError::kWorkLimit ? Error::kWorkLimit : Error::kResourceLimit;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
