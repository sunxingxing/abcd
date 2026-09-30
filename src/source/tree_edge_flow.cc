#include "tree_edge_flow.h"
#include "completion_tree.h"

namespace abcd::source {
namespace {
void Require(bool condition, TreeFlowError error) { if (!condition) throw error; }
}  // namespace

TreeEdgeFlow::TreeEdgeFlow(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, const FunctionTree& tree, TreeFlow& flow,
    std::vector<std::uint8_t>& declared, std::vector<std::uint8_t>& assigned,
    TreeCheckBudget& budget, const CompletionTreeView& sequences)
    : graph_(graph), evaluation_(evaluation), tree_(tree), sequences_(sequences), flow_(flow),
      declared_(declared), assigned_(assigned), budget_(budget) {
  if (!evaluation.phis) return;
  budget.Array(flow.phis, graph.values.size());
  budget.Array(bundles_, graph.edges.size());
  budget.Array(flow.copies, evaluation.phis->bundles.size());
  budget.Array(visited_, evaluation.phis->bundles.size());
  if (evaluation.exceptions) {
    budget.Array(throws_, graph.ops.size());
    budget.Array(catches_, graph.blocks.size());
    budget.Array(flow.catch_slots, evaluation.exceptions->slots.size());
    budget.Array(flow.catch_commits, evaluation.exceptions->commits.size());
    budget.Array(committed_, evaluation.exceptions->commits.size());
    for (std::size_t i = 0; i < evaluation.exceptions->commits.size(); ++i) {
      budget.Step();
      const auto& commit = evaluation.exceptions->commits[i];
      Require(!catches_[commit.block.value], TreeFlowError::kControl);
      catches_[commit.block.value] = i;
      budget.Array(flow.catch_commits[i], commit.count);
    }
  }
  for (std::size_t i = 0; i < evaluation.phis->bundles.size(); ++i) {
    budget.Step();
    const auto& bundle = evaluation.phis->bundles[i];
    const auto* edge = std::get_if<analysis::EdgeId>(&bundle.incoming);
    if (edge) {
      Require(edge->value < bundles_.size() && !bundles_[edge->value], TreeFlowError::kControl);
      bundles_[edge->value] = i;
    } else {
      Require(!entry_, TreeFlowError::kControl);
      entry_ = i;
    }
    auto& copies = flow.copies[i];
    copies.incoming = bundle.incoming;
    if (evaluation.exceptions) {
      if (const auto* stage = std::get_if<ThrowCopies>(&evaluation.exceptions->placements[i])) {
        Require(!throws_[stage->operation.value], TreeFlowError::kControl);
        throws_[stage->operation.value] = i;
        budget.Array(copies.expressions, bundle.count);
        continue;
      }
    }
    budget.Array(copies.expressions, bundle.schedule.steps.size());
    budget.Array(copies.temporaries, bundle.schedule.temporaries);
  }
}

void TreeEdgeFlow::Bind(std::optional<SymbolId>& binding, SymbolId symbol,
    TemporaryReason reason) {
  Require(symbol.value < tree_.symbols.size(), TreeFlowError::kSnapshot);
  const auto& declared = tree_.symbols[symbol.value];
  Require(declared.kind == BindingKind::kCompilerTemporary &&
      declared.scope == tree_.body_scope && declared.declaration == DeclarationKind::kLet &&
      declared.temporary == reason && declared_[symbol.value], TreeFlowError::kSnapshot);
  if (binding) {
    Require(*binding == symbol, TreeFlowError::kSnapshot);
  } else {
    // Different phi values, cycle saves, and ordinary snapshots cannot share
    // one provisional location. Coalescing requires a later interference proof.
    Require(!assigned_[symbol.value], TreeFlowError::kSnapshot);
    binding = symbol;
    assigned_[symbol.value] = 1;
  }
}

void TreeEdgeFlow::Consume(analysis::IncomingKey incoming, const BlockStmt& block,
    std::uint32_t& child) {
  budget_.Step();
  if (!evaluation_.phis) return;
  std::optional<std::size_t> bundle;
  const auto* edge = std::get_if<analysis::EdgeId>(&incoming);
  if (edge) {
    Require(edge->value < bundles_.size(), TreeFlowError::kControl);
    bundle = bundles_[edge->value];
  } else bundle = entry_;
  if (!bundle) return;
  Require(!evaluation_.exceptions ||
      !std::holds_alternative<ThrowCopies>(evaluation_.exceptions->placements[*bundle]),
      TreeFlowError::kSequence);
  Require(!visited_[*bundle], TreeFlowError::kSequence);
  visited_[*bundle] = 1;
  const auto& schedule = evaluation_.phis->bundles[*bundle].schedule;
  auto& copies = flow_.copies[*bundle];
  for (std::size_t i = 0; i < schedule.steps.size(); ++i) {
    budget_.Step();
    const auto expression = Assignment(incoming, block, child, true);
    const auto& assignment = std::get<AssignmentExpr>(tree_.expressions[expression.value].expression);
    const auto& target = std::get<SymbolTarget>(assignment.target);
    const auto& destination = schedule.steps[i].destination;
    if (const auto* value = std::get_if<analysis::ValueId>(&destination)) {
      Require(value->value < flow_.phis.size(), TreeFlowError::kSnapshot);
      Bind(flow_.phis[value->value], target.symbol, TemporaryReason::kPhiJoin);
    } else {
      const auto temporary = std::get<CopyTempId>(destination);
      Require(temporary.value < copies.temporaries.size(), TreeFlowError::kSnapshot);
      Bind(copies.temporaries[temporary.value], target.symbol, TemporaryReason::kParallelCopyCycle);
    }
    if (tree_.expressions[assignment.value.value].origin != tree_.expressions[expression.value].origin) {
      const auto* operand = std::get_if<analysis::ValueOperand>(&schedule.steps[i].source);
      const auto* value = operand ? std::get_if<analysis::ValueId>(operand) : nullptr;
      const auto* producer = value ? std::get_if<analysis::OpResult>(&graph_.values[value->value].origin) : nullptr;
      Require(producer && evaluation_.operations[producer->op.value].kind == EvaluationKind::kPhiInput &&
          flow_.expressions[producer->op.value] == assignment.value, TreeFlowError::kPlacement);
    }
    copies.expressions[i] = expression;
  }
}

void TreeEdgeFlow::Select(analysis::EdgeId incoming, SymbolId target, ExprId value) {
  budget_.Step();
  Require(incoming.value < bundles_.size() && bundles_[incoming.value], TreeFlowError::kControl);
  const auto id = *bundles_[incoming.value];
  const auto& bundle = evaluation_.phis->bundles[id];
  Require(!visited_[id] && !evaluation_.exceptions && bundle.count == 1 &&
      bundle.schedule.steps.size() == 1 && bundle.schedule.temporaries == 0, TreeFlowError::kSequence);
  const auto* phi = std::get_if<analysis::ValueId>(&bundle.schedule.steps[0].destination);
  Require(phi != nullptr, TreeFlowError::kSnapshot);
  Bind(flow_.phis[phi->value], target, TemporaryReason::kPhiJoin);
  visited_[id] = 1;
  auto& copies = flow_.copies[id]; copies.expressions.clear(); copies.selected = value;
}

bool TreeEdgeFlow::HasCopies(analysis::EdgeId edge) const {
  if (bundles_.empty() || !bundles_[edge.value]) return false;
  return !flow_.copies[*bundles_[edge.value]].expressions.empty();
}

ExprId TreeEdgeFlow::Assignment(analysis::IncomingKey incoming, const BlockStmt& block,
    std::uint32_t& child, bool allow_phi_expression) {
  Require(child < block.child_count, TreeFlowError::kSequence);
  const auto& statement = tree_.statements[sequences_.Child(block.first_child + child++).value];
  const auto* expression = std::get_if<ExpressionStmt>(&statement.statement);
  Require(expression != nullptr, TreeFlowError::kSequence);
  const auto& node = tree_.expressions[expression->expression.value];
  const auto& origin = tree_.origins[node.origin.value];
  if (const auto* edge = std::get_if<analysis::EdgeId>(&incoming)) {
    const auto* source = std::get_if<EdgeOrigin>(&origin);
    Require(source && source->edge == *edge, TreeFlowError::kPlacement);
  } else Require(std::holds_alternative<EntryOrigin>(origin), TreeFlowError::kPlacement);
  Require(statement.origin == node.origin, TreeFlowError::kPlacement);
  const auto* assignment = std::get_if<AssignmentExpr>(&node.expression);
  Require(assignment && std::holds_alternative<SymbolTarget>(assignment->target) &&
      (allow_phi_expression || tree_.expressions[assignment->value.value].origin == node.origin),
      TreeFlowError::kPlacement);
  return expression->expression;
}

void TreeEdgeFlow::Stage(analysis::OpId operation, const BlockStmt& block,
    std::uint32_t& child) {
  budget_.Step();
  if (throws_.empty() || !throws_[operation.value]) return;
  const auto index = *throws_[operation.value];
  Require(!visited_[index], TreeFlowError::kSequence);
  visited_[index] = 1;
  const auto& bundle = evaluation_.phis->bundles[index];
  for (std::uint32_t i = 0; i < bundle.count; ++i) {
    budget_.Step();
    const auto slot = evaluation_.exceptions->staging[bundle.first + i];
    Require(slot.has_value(), TreeFlowError::kSnapshot);
    const auto expression = Assignment(bundle.incoming, block, child);
    const auto& assignment = std::get<AssignmentExpr>(tree_.expressions[expression.value].expression);
    Bind(flow_.catch_slots[slot->value], std::get<SymbolTarget>(assignment.target).symbol,
        TemporaryReason::kExceptionSnapshot);
    flow_.copies[index].expressions[i] = expression;
  }
}

void TreeEdgeFlow::Catch(analysis::WorkBlockId handler, const BlockStmt& block,
    std::uint32_t& child) {
  budget_.Step();
  if (catches_.empty() || !catches_[handler.value]) return;
  const auto index = *catches_[handler.value];
  Require(!committed_[index], TreeFlowError::kSequence);
  committed_[index] = 1;
  const auto& commit = evaluation_.exceptions->commits[index];
  const auto& source = graph_.blocks[handler.value];
  Require(source.edge_count == 1, TreeFlowError::kControl);
  for (std::uint32_t i = 0; i < commit.count; ++i) {
    budget_.Step();
    const auto slot = commit.first.value + i;
    const auto destination = evaluation_.exceptions->slots[slot].destination;
    const auto expression = Assignment(source.first_edge, block, child);
    const auto& assignment = std::get<AssignmentExpr>(tree_.expressions[expression.value].expression);
    Bind(flow_.phis[destination.value], std::get<SymbolTarget>(assignment.target).symbol,
        TemporaryReason::kPhiJoin);
    const auto* saved = std::get_if<SymbolExpr>(&tree_.expressions[assignment.value.value].expression);
    Require(saved != nullptr, TreeFlowError::kSnapshot);
    Bind(flow_.catch_slots[slot], saved->symbol, TemporaryReason::kExceptionSnapshot);
    flow_.catch_commits[index][i] = expression;
  }
}

void TreeEdgeFlow::Finish() {
  if (!evaluation_.phis) return;
  for (const auto visited : visited_) {
    budget_.Step();
    Require(visited, TreeFlowError::kSequence);
  }
  for (const auto visited : committed_) {
    budget_.Step(); Require(visited, TreeFlowError::kSequence);
  }
  for (const auto slot : flow_.catch_slots) {
    budget_.Step(); Require(slot.has_value(), TreeFlowError::kSnapshot);
  }
  for (std::size_t i = 0; i < graph_.values.size(); ++i) {
    budget_.Step();
    const auto& value = graph_.values[i];
    const bool phi = value.life == analysis::ValueLife::kLive &&
        std::holds_alternative<analysis::PhiDefinition>(value.origin);
    Require(flow_.phis[i].has_value() == phi, TreeFlowError::kSnapshot);
  }
}
}  // namespace abcd::source
