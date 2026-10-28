#include "tree_edge_flow.h"
#include "for_in_emission.h"
#include "for_of_evaluation.h"
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
  budget.Array(flow.shared_stages, evaluation.exception_emission.assignments.size());
  budget.Array(shared_staged_, evaluation.exception_emission.groups.size());
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
        bool ordinary=false;
        for(std::size_t p=bundle.first;p<std::size_t(bundle.first)+bundle.count;++p) {
          budget.Step();ordinary|=!ProtocolConsumedCopy(*evaluation.exceptions,*evaluation.phis,p);
        }
        if(ordinary) {
          if (!evaluation.exception_emission.bundles.empty() && evaluation.exception_emission.bundles[i]) {
            copies.stage_group = evaluation.exception_emission.bundles[i];
            visited_[i] = 1;  // Relation coverage, not a second physical statement.
          } else {
            Require(!throws_[stage->operation.value], TreeFlowError::kControl);
            throws_[stage->operation.value] = i;
            budget.Array(copies.expressions, bundle.count);
          }
        }
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
  ConsumeImpl(incoming, block, child, nullptr);
}
void TreeEdgeFlow::Decision(analysis::EdgeId incoming, ExprId expression, bool result) {
  std::optional<ExprId> sequence{expression}; std::uint32_t empty = 0;
  ConsumeImpl(incoming, BlockStmt{}, empty, &sequence);
  const auto* literal = std::get_if<TreeLiteral>(&tree_.expressions[sequence->value].expression);
  const auto* boolean = literal ? std::get_if<BooleanLiteral>(literal) : nullptr;
  Require(boolean && boolean->value == result, TreeFlowError::kControl);
}
void TreeEdgeFlow::ConsumeImpl(analysis::IncomingKey incoming, const BlockStmt& block,
    std::uint32_t& child, std::optional<ExprId>* sequence) {
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
    ExprId expression{};
    if (sequence) {
      const auto* comma = std::get_if<SequenceExpr>(&tree_.expressions[(*sequence)->value].expression);
      Require(comma, TreeFlowError::kSequence);
      expression = comma->left; *sequence = comma->right;
      CheckAssignment(incoming, expression, true);
    } else expression = Assignment(incoming, block, child, true);
    const auto& assignment = std::get<AssignmentExpr>(tree_.expressions[expression.value].expression);
    const auto& target = std::get<SymbolTarget>(assignment.target);
    const auto& destination = schedule.steps[i].destination;
    if (const auto* value = std::get_if<analysis::ValueId>(&destination)) {
      Require(value->value < flow_.phis.size(), TreeFlowError::kSnapshot);
      Bind(flow_.phis[value->value], target.symbol, TemporaryReason::kPhiJoin);
      flow_.definitions[expression.value]=OriginalSourceDefinition{
          OriginalPhiDefinition{*value,incoming},target.symbol,assignment.value};
    } else {
      const auto temporary = std::get<CopyTempId>(destination);
      Require(temporary.value < copies.temporaries.size(), TreeFlowError::kSnapshot);
      Bind(copies.temporaries[temporary.value], target.symbol, TemporaryReason::kParallelCopyCycle);
      if(const auto* operand=std::get_if<analysis::ValueOperand>(&schedule.steps[i].source))
        flow_.definitions[expression.value]=OriginalSourceDefinition{
            OriginalSavedDefinition{*operand,incoming},target.symbol,assignment.value};
    }
    if (tree_.expressions[assignment.value.value].origin != tree_.expressions[expression.value].origin) {
      const auto* operand = std::get_if<analysis::ValueOperand>(&schedule.steps[i].source);
      // The schedule retains its original operand identity. Its source
      // expression may be owned by the already checked canonical producer.
      const auto* value = operand ? std::get_if<analysis::ValueId>(operand) : nullptr;
      const auto* producer = value ? std::get_if<analysis::OpResult>(
          &graph_.values[ResolveEvaluationValue(evaluation_, *value).value].origin) : nullptr;
      Require(producer && evaluation_.operations[producer->op.value].kind == EvaluationKind::kPhiInput &&
          flow_.expressions[producer->op.value] == assignment.value, TreeFlowError::kPlacement);
    }
    copies.expressions[i] = expression;
  }
}

void TreeEdgeFlow::Select(analysis::EdgeId incoming, analysis::ValueId expected, std::optional<SymbolId> target, ExprId value) {
  budget_.Step();
  Require(incoming.value < bundles_.size() && bundles_[incoming.value], TreeFlowError::kControl);
  const auto id = *bundles_[incoming.value];
  const auto& bundle = evaluation_.phis->bundles[id];
  Require(!visited_[id] && bundle.expression_input &&
      bundle.schedule.steps.size() == 1 && bundle.schedule.temporaries == 0, TreeFlowError::kSequence);
  if (evaluation_.exceptions) {
    const auto* normal = std::get_if<NormalCopies>(&evaluation_.exceptions->placements[id]);
    Require(normal && normal->edge == incoming, TreeFlowError::kSequence);
  }
  const auto* phi = std::get_if<analysis::ValueId>(&bundle.schedule.steps[0].destination);
  Require(phi && *phi == expected, TreeFlowError::kSnapshot);
  const bool fused = !evaluation_.logical_values.empty() && evaluation_.logical_values[phi->value].has_value();
  Require(target.has_value() != fused, TreeFlowError::kSnapshot);
  if (target) Bind(flow_.phis[phi->value], *target, TemporaryReason::kPhiJoin);
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
  CheckAssignment(incoming, expression->expression, allow_phi_expression);
  Require(statement.origin == tree_.expressions[expression->expression.value].origin, TreeFlowError::kPlacement);
  return expression->expression;
}
void TreeEdgeFlow::CheckAssignment(analysis::IncomingKey incoming, ExprId expression, bool allow_phi_expression) {
  const auto& node = tree_.expressions[expression.value];
  const auto& origin = tree_.origins[node.origin.value];
  if (const auto* edge = std::get_if<analysis::EdgeId>(&incoming)) {
    const auto* source = std::get_if<EdgeOrigin>(&origin);
    Require(source && source->edge == *edge, TreeFlowError::kPlacement);
  } else Require(std::holds_alternative<EntryOrigin>(origin), TreeFlowError::kPlacement);
  const auto* assignment = std::get_if<AssignmentExpr>(&node.expression);
  Require(assignment && std::holds_alternative<SymbolTarget>(assignment->target) &&
      (allow_phi_expression || tree_.expressions[assignment->value.value].origin == node.origin),
      TreeFlowError::kPlacement);
}

void TreeEdgeFlow::Stage(analysis::OpId operation, const BlockStmt& block,
    std::uint32_t& child) {
  budget_.Step();
  const auto& emission = evaluation_.exception_emission;
  if (!emission.entries.empty()) if (const auto g = emission.entries[operation.value]) {
    Require(!shared_staged_[*g], TreeFlowError::kSequence); shared_staged_[*g] = 1;
    const auto& group = emission.groups[*g];
    const auto incoming = evaluation_.phis->bundles[group.bundle].incoming;
    for (std::uint32_t j = 0; j < group.count; ++j) {
      budget_.Step(); const auto a = group.first + j;
      const auto& expected = emission.assignments[a];
      const auto expression = Assignment(incoming, block, child);
      const auto& assignment = std::get<AssignmentExpr>(tree_.expressions[expression.value].expression);
      const auto symbol = std::get<SymbolTarget>(assignment.target).symbol;
      Bind(flow_.catch_slots[expected.slot.value], symbol, TemporaryReason::kExceptionSnapshot);
      flow_.shared_stages[a] = expression;
      flow_.definitions[expression.value] = OriginalSourceDefinition{
          OriginalSavedDefinition{expected.source, group.entry}, symbol, assignment.value};
    }
  }
  if (throws_.empty() || !throws_[operation.value]) return;
  const auto index = *throws_[operation.value];
  Require(!visited_[index], TreeFlowError::kSequence);
  visited_[index] = 1;
  const auto& bundle = evaluation_.phis->bundles[index];
  for (std::uint32_t i = 0; i < bundle.count; ++i) {
    budget_.Step();
    if(ProtocolConsumedCopy(*evaluation_.exceptions,*evaluation_.phis,bundle.first+i))continue;
    const auto slot = evaluation_.exceptions->staging[bundle.first + i];
    Require(slot.has_value(), TreeFlowError::kSnapshot);
    const auto expression = Assignment(bundle.incoming, block, child);
    const auto& assignment = std::get<AssignmentExpr>(tree_.expressions[expression.value].expression);
    Bind(flow_.catch_slots[slot->value], std::get<SymbolTarget>(assignment.target).symbol,
        TemporaryReason::kExceptionSnapshot);
    flow_.copies[index].expressions[i] = expression;
    flow_.definitions[expression.value]=OriginalSourceDefinition{
        OriginalSavedDefinition{evaluation_.phis->copies[bundle.first+i].source,operation},
        std::get<SymbolTarget>(assignment.target).symbol,assignment.value};
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
    flow_.definitions[expression.value]=OriginalSourceDefinition{
        OriginalCatchDefinition{destination,handler},std::get<SymbolTarget>(assignment.target).symbol,assignment.value};
  }
}

void TreeEdgeFlow::Forwarded(const ForInEmissionView& view) {
  if (!evaluation_.phis) return;
  for (std::size_t b = 0; b < evaluation_.phis->bundles.size(); ++b) {
    budget_.Step(); const auto* id = std::get_if<analysis::EdgeId>(&evaluation_.phis->bundles[b].incoming);
    if (!id) continue;
    const auto& edge = graph_.edges[id->value];
    if (!view.forwarded_blocks[edge.source.value] && !view.forwarded_blocks[edge.target.value]) continue;
    Require(flow_.copies[b].expressions.empty() && flow_.copies[b].temporaries.empty(), TreeFlowError::kSequence);
    visited_[b] = 1;
  }
  if (evaluation_.exceptions) for (std::size_t c = 0; c < evaluation_.exceptions->commits.size(); ++c) {
    budget_.Step(); const auto& commit = evaluation_.exceptions->commits[c];
    if (!view.forwarded_blocks[commit.block.value]) continue;
    Require(commit.count == 0, TreeFlowError::kSequence); committed_[c] = 1;
  }
}
void TreeEdgeFlow::ProtocolOwned(const ForOfEmissionView& view) {
  if(!evaluation_.phis)return;
  for(std::size_t b=0;b<evaluation_.phis->bundles.size();++b) {
    budget_.Step();const auto* id=std::get_if<analysis::EdgeId>(&evaluation_.phis->bundles[b].incoming);if(!id)continue;
    const auto& edge=graph_.edges[id->value];
    if(!view.close_blocks[edge.source.value] && !view.close_blocks[edge.target.value])continue;
    Require(flow_.copies[b].expressions.empty() && flow_.copies[b].temporaries.empty(),TreeFlowError::kSequence);visited_[b]=1;
  }
  if(evaluation_.exceptions)for(std::size_t c=0;c<evaluation_.exceptions->commits.size();++c) {
    budget_.Step();const auto& commit=evaluation_.exceptions->commits[c];if(!view.close_blocks[commit.block.value])continue;
    Require(commit.count==0,TreeFlowError::kSequence);committed_[c]=1;
  }
}
void TreeEdgeFlow::Finish() {
  for (const auto staged : shared_staged_) { budget_.Step(); Require(staged, TreeFlowError::kSequence); }

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
    const bool phi = (evaluation_.aliases.empty() || evaluation_.aliases[i] == analysis::ValueId{static_cast<std::uint32_t>(i)}) && value.life == analysis::ValueLife::kLive &&
        std::holds_alternative<analysis::PhiDefinition>(value.origin);
    Require(flow_.phis[i].has_value() == (phi && (!evaluation_.for_in || !evaluation_.for_in->values[i]) && !ForOfValue(evaluation_,{static_cast<std::uint32_t>(i)}) && (evaluation_.logical_values.empty() || !evaluation_.logical_values[i])), TreeFlowError::kSnapshot);
  }
}
}  // namespace abcd::source
