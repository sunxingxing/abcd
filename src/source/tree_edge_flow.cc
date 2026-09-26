#include "tree_edge_flow.h"

namespace abcd::source {
namespace {
void Require(bool condition, TreeFlowError error) { if (!condition) throw error; }
}  // namespace

TreeEdgeFlow::TreeEdgeFlow(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, const FunctionTree& tree, TreeFlow& flow,
    std::vector<std::uint8_t>& declared, std::vector<std::uint8_t>& assigned,
    TreeCheckBudget& budget)
    : graph_(graph), evaluation_(evaluation), tree_(tree), flow_(flow),
      declared_(declared), assigned_(assigned), budget_(budget) {
  if (!evaluation.phis) return;
  budget.Array(flow.phis, graph.values.size());
  budget.Array(bundles_, graph.edges.size());
  budget.Array(flow.copies, evaluation.phis->bundles.size());
  budget.Array(visited_, evaluation.phis->bundles.size());
  for (std::size_t i = 0; i < evaluation.phis->bundles.size(); ++i) {
    budget.Step();
    const auto& bundle = evaluation.phis->bundles[i];
    const auto* edge = std::get_if<analysis::EdgeId>(&bundle.incoming);
    Require(edge && edge->value < bundles_.size() && !bundles_[edge->value], TreeFlowError::kControl);
    bundles_[edge->value] = i;
    auto& copies = flow.copies[i];
    copies.edge = *edge;
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

void TreeEdgeFlow::Consume(analysis::EdgeId edge, const BlockStmt& block,
    std::uint32_t& child) {
  budget_.Step();
  if (!evaluation_.phis) return;
  Require(edge.value < bundles_.size(), TreeFlowError::kControl);
  const auto bundle = bundles_[edge.value];
  if (!bundle) return;
  Require(!visited_[*bundle], TreeFlowError::kSequence);
  visited_[*bundle] = 1;
  const auto& schedule = evaluation_.phis->bundles[*bundle].schedule;
  auto& copies = flow_.copies[*bundle];
  for (std::size_t i = 0; i < schedule.steps.size(); ++i) {
    budget_.Step();
    Require(child < block.child_count, TreeFlowError::kSequence);
    const auto& statement = tree_.statements[tree_.children[block.first_child + child++].value];
    const auto* expression = std::get_if<ExpressionStmt>(&statement.statement);
    Require(expression != nullptr, TreeFlowError::kSequence);
    const auto& node = tree_.expressions[expression->expression.value];
    const auto* origin = std::get_if<EdgeOrigin>(&tree_.origins[node.origin.value]);
    Require(origin && origin->edge == edge && statement.origin == node.origin, TreeFlowError::kPlacement);
    const auto* assignment = std::get_if<AssignmentExpr>(&node.expression);
    Require(assignment != nullptr, TreeFlowError::kSequence);
    const auto* target = std::get_if<SymbolTarget>(&assignment->target);
    Require(target && tree_.expressions[assignment->value.value].origin == node.origin, TreeFlowError::kPlacement);
    const auto& destination = schedule.steps[i].destination;
    if (const auto* value = std::get_if<analysis::ValueId>(&destination)) {
      Require(value->value < flow_.phis.size(), TreeFlowError::kSnapshot);
      Bind(flow_.phis[value->value], target->symbol, TemporaryReason::kPhiJoin);
    } else {
      const auto temporary = std::get<CopyTempId>(destination);
      Require(temporary.value < copies.temporaries.size(), TreeFlowError::kSnapshot);
      Bind(copies.temporaries[temporary.value], target->symbol, TemporaryReason::kParallelCopyCycle);
    }
    copies.expressions[i] = expression->expression;
  }
}

void TreeEdgeFlow::Finish() {
  if (!evaluation_.phis) return;
  for (const auto visited : visited_) {
    budget_.Step();
    Require(visited, TreeFlowError::kSequence);
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
