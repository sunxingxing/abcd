#include "tree_correspondence.h"

namespace abcd::source {
namespace {
using Error = TreeSemanticError;
struct OriginOwner {
  std::optional<ExprId> expression;
  // A selected value labels its enclosing decision with the branch site;
  // the original scalar predicate still has its own execution expression.
  std::optional<ExprId> selection;
};
template <class Origin>
bool SameSite(const Origin& tree, const analysis::ValueOrigin& input) {
  return tree.site == input.site && tree.pc == input.pc && tree.original == input.original;
}
}  // namespace

void CheckTreeCorrespondenceOrigins(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, const FunctionTree& tree, const TreeFlow& flow, TreeCheckBudget& budget) {
  RequireMeaning(tree.origins.size() >= graph.origins.size(), Error::kOrigin);
  for (std::size_t i = 0; i < graph.origins.size(); ++i) {
    budget.Step();
    const auto& original = graph.origins[i];
    if (original.disposition == analysis::OriginDisposition::kRetained) {
      const auto* site = std::get_if<OriginalSite>(&tree.origins[i]);
      RequireMeaning(site && SameSite(*site, original), Error::kOrigin);
    } else if (original.disposition == analysis::OriginDisposition::kElidedPure) {
      const auto* site = std::get_if<ElidedOrigin>(&tree.origins[i]);
      RequireMeaning(site && SameSite(*site, original), Error::kOrigin);
    } else throw Error::kOrigin;
  }
  std::vector<OriginOwner> expected;
  std::vector<std::uint8_t> covered;
  std::vector<std::uint8_t> visited;
  std::vector<TreeOriginId> stack;
  std::vector<std::uint8_t> edge_expressions;
  std::vector<std::uint8_t> completion_expressions;
  if (!flow.completion_expressions.empty()) {
    budget.Array(completion_expressions, tree.expressions.size());
    for (const auto id : flow.completion_expressions) completion_expressions[id.value] = 1;
  }
  budget.Admit(1, sizeof(expected) + sizeof(covered) + sizeof(visited) + sizeof(stack) + sizeof(edge_expressions));
  budget.Array(expected, graph.origins.size());
  budget.Array(covered, graph.origins.size());
  budget.Array(visited, tree.origins.size());
  budget.Array(stack, tree.origin_parts.size() + 1);
  for (std::size_t c=0;c<flow.controls.size();++c) {
    const auto origin=flow.controls[c];
    budget.Step();
    if(!flow.enumeration_controls.empty() && flow.enumeration_controls[c]) {
      if(*flow.enumeration_controls[c]==EnumerationControl::kLoop) {
        RequireMeaning(flow.control_statements[c].value<tree.statements.size() && origin.value<graph.origins.size() &&
            std::holds_alternative<ForInStmt>(tree.statements[flow.control_statements[c].value].statement),Error::kOrigin);
      }
      continue; // Original done edge is the native exhausted port, not a fake break statement.
    }
    RequireMeaning(origin.value >= graph.origins.size() && origin.value < tree.origins.size() &&
        !visited[origin.value] &&
        (std::holds_alternative<LoopOrigin>(tree.origins[origin.value]) ||
         std::holds_alternative<TryOrigin>(tree.origins[origin.value]) ||
         std::holds_alternative<EdgeOrigin>(tree.origins[origin.value])), Error::kOrigin);
    visited[origin.value] = 1;
  }
  for (std::size_t i = 0; i < flow.tries.size(); ++i) {
    budget.Step();
    if (!flow.tries[i]) continue; // Only freshly verified forwarded routes have no physical try.
    const auto origin = tree.statements[flow.tries[i]->value].origin;
    const auto* region = std::get_if<TryOrigin>(&tree.origins[origin.value]);
    RequireMeaning(origin.value >= graph.origins.size() && region && region->region.value == i &&
        !visited[origin.value], Error::kOrigin);
    visited[origin.value] = 1;
  }
  if (!flow.copies.empty() || !flow.catch_commits.empty())
    budget.Array(edge_expressions, tree.expressions.size());
  const auto mark_copy = [&](ExprId root) {
    const auto& assignment = std::get<AssignmentExpr>(tree.expressions[root.value].expression);
    // Fresh flow checked placement/targets, operands proved the snapshot RHS;
    // catch commit RHS is a slot leaf checked directly by fresh flow.
    RequireMeaning(!edge_expressions[root.value] && !edge_expressions[assignment.value.value], Error::kOrigin);
    edge_expressions[root.value] = 1;
    edge_expressions[assignment.value.value] = 1;
  };
  for (const auto& copies : flow.copies) {
    budget.Step();
    for (const auto root : copies.expressions) {
      budget.Step();
      const auto& node = tree.expressions[root.value];
      const auto& origin = tree.origins[node.origin.value];
      if (const auto* edge = std::get_if<analysis::EdgeId>(&copies.incoming)) {
        const auto* source = std::get_if<EdgeOrigin>(&origin);
        RequireMeaning(source && source->edge == *edge, Error::kOrigin);
      } else RequireMeaning(std::holds_alternative<EntryOrigin>(origin), Error::kOrigin);
      mark_copy(root);
    }
  }
  for (const auto& commit : flow.catch_commits) {
    budget.Step();
    for (const auto root : commit) {
      budget.Step();
      RequireMeaning(std::holds_alternative<EdgeOrigin>(tree.origins[tree.expressions[root.value].origin.value]),
          Error::kOrigin);
      mark_copy(root);
    }
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    const auto& op = graph.ops[i];
    if ((evaluation.for_in && evaluation.for_in->operations[i]) ||
        (!flow.forwarded_operations.empty() && flow.forwarded_operations[i])) continue;
    if (IsReceiverGuard(tree, analysis::OpId{static_cast<std::uint32_t>(i)}) || evaluation.operations[i].kind == EvaluationKind::kCellGuard || evaluation.operations[i].kind == EvaluationKind::kArrayCursor || evaluation.operations[i].kind == EvaluationKind::kClassProjection || IsAbsentHeritageTransport(graph, evaluation, op) || op.life != analysis::ValueLife::kLive ||
        evaluation.operations[i].kind == EvaluationKind::kElidedPure ||
        evaluation.operations[i].kind == EvaluationKind::kAlias) continue;
    RequireMeaning(op.origin.value < graph.origins.size(), Error::kOrigin);
    const TreeOriginId origin{op.origin.value};
    if (flow.statements[i])
      RequireMeaning(tree.statements[flow.statements[i]->value].origin == origin, Error::kOrigin);
    if (flow.snapshots[i] && flow.statements[i] && evaluation.operations[i].kind != EvaluationKind::kPredicateSnapshot) {
      const auto& statement = std::get<ExpressionStmt>(tree.statements[flow.statements[i]->value].statement);
      RequireMeaning(tree.expressions[statement.expression.value].origin == origin, Error::kOrigin);
    }
    if (std::holds_alternative<analysis::ValueJump>(op.operation) ||
        std::holds_alternative<analysis::ValueDebugger>(op.operation) ||
        std::holds_alternative<analysis::ValueThrow>(op.operation) ||
        std::holds_alternative<analysis::ValueReturn>(op.operation) || IsEnvironmentState(op.operation) ||
        ((std::holds_alternative<analysis::ValueModuleWrite>(op.operation) ||
          std::holds_alternative<analysis::ValueLexicalStore>(op.operation)) && flow.statements[i] &&
         std::holds_alternative<DeclarationStmt>(tree.statements[flow.statements[i]->value].statement))) continue;
    // Selected staged definitions keep their store PC on the DataProperty;
    // the conditional child retains its separately verified branch origin.
    if (evaluation.operations[i].kind == EvaluationKind::kInitializer &&
        IsStagedLiteral(evaluation.literals ? &*evaluation.literals : nullptr,
            analysis::OpId{static_cast<std::uint32_t>(i)})) continue;
    if (evaluation.operations[i].kind == EvaluationKind::kInitializer && !IsArrayInitialization(evaluation, {static_cast<std::uint32_t>(i)}) && evaluation.literals &&
        !evaluation.literals->nested.empty()) {
      const analysis::UseId rhs{op.first_use.value + 1};
      const auto operand = ResolveEvaluationOperand(evaluation, graph.uses[rhs.value].operand);
      const auto* value = std::get_if<analysis::ValueId>(&operand);
      const auto* result = value ? std::get_if<analysis::OpResult>(&graph.values[value->value].origin) : nullptr;
      // The DataProperty owns the store site (checked by TreeFlow). Its child
      // allocation expression retains its own original site, not the store PC.
      if (result && evaluation.literals->nested[result->op.value] == rhs) continue;
    }
    RequireMeaning(flow.expressions[i].has_value(), Error::kOrigin);
    auto& owner = expected[op.origin.value].expression;
    // A decision-selection branch owns its original scalar test. The selected
    // value is separately checked by TreeFlow against the conditional syntax.
    const auto expression = !flow.branch_predicates.empty() && flow.branch_predicates[i]
        ? flow.branch_predicates[i] : flow.expressions[i];
    RequireMeaning(!owner || owner == expression, Error::kOrigin);
    owner = expression;
    if (expression != flow.expressions[i]) {
      RequireMeaning(std::holds_alternative<analysis::ValueBranch>(op.operation), Error::kOrigin);
      auto& selection = expected[op.origin.value].selection;
      RequireMeaning(!selection || selection == flow.expressions[i], Error::kOrigin);
      selection = flow.expressions[i];
    }
  }
  for (const auto& placement : tree.operations) {
    budget.Step();
    if (const auto* anchor = std::get_if<AnchorId>(&placement.node))
      RequireMeaning(tree.anchors[anchor->value].origin.value == graph.ops[placement.operation.value].origin.value,
                     Error::kOrigin);
  }
  for (const auto& statement : tree.statements) {
    budget.Step();
    if (const auto* declaration = std::get_if<DeclarationStmt>(&statement.statement)) {
      // Fresh TreeFlow consumed every local exactly at its allocation (or, for a
      // deferred cell, at its first store) and checked this original origin.
      // Several slots may share one allocation.
      if (tree.symbols[declaration->symbol.value].kind == BindingKind::kLocal) continue;
      const auto* origin = std::get_if<BindingOrigin>(&tree.origins[statement.origin.value]);
      RequireMeaning(origin && origin->symbol == declaration->symbol && !visited[statement.origin.value], Error::kOrigin);
      visited[statement.origin.value] = 1;
    }
  }
  for (std::size_t i = 0; i < tree.expressions.size(); ++i) {
    budget.Step();
    if (flow.hole_initializer == ExprId{static_cast<std::uint32_t>(i)}) {
      visited[tree.expressions[i].origin.value] = 1;
      continue;
    }
    if (!completion_expressions.empty() && completion_expressions[i]) continue;
    const ExprId expression{static_cast<std::uint32_t>(i)};
    const auto origin = tree.expressions[i].origin;
    if (std::holds_alternative<EdgeOrigin>(tree.origins[origin.value]) ||
        std::holds_alternative<EntryOrigin>(tree.origins[origin.value])) {
      RequireMeaning(!edge_expressions.empty() && edge_expressions[i], Error::kOrigin);
      visited[origin.value] = 1;
      continue;
    }
    if (origin.value < graph.origins.size()) {
      if (expected[origin.value].expression == expression) covered[origin.value] = 1;
      continue;
    }
    std::size_t pending = 1;
    stack[0] = origin;
    while (pending != 0) {
      budget.Step();
      const auto part = stack[--pending];
      if (part.value < graph.origins.size()) {
        const auto& owner = expected[part.value];
        if (owner.expression == expression) covered[part.value] = 1;
        else {
          // A transparent conversion may combine with the selected value's
          // contextual branch label. Fresh flow proves that exact selection;
          // it cannot cover the original predicate's separate execution site.
          RequireMeaning(owner.selection == expression, Error::kOrigin);
        }
        continue;
      }
      const auto* combined = std::get_if<CombinedOrigin>(&tree.origins[part.value]);
      // Every combined origin belongs to one expression root. Backward DAG
      // sharing of input sites is fine; sharing combined chains across syntax
      // roots would create ambiguous execution ownership and repeated walks.
      RequireMeaning(combined && !visited[part.value], Error::kOrigin);
      visited[part.value] = 1;
      for (std::uint32_t j = 0; j < combined->part_count; ++j) {
        budget.Step();
        RequireMeaning(pending < stack.size(), Error::kResourceLimit);
        stack[pending++] = tree.origin_parts[combined->first_part + j];
      }
    }
  }
  for (std::size_t i = 0; i < expected.size(); ++i) {
    budget.Step();
    RequireMeaning(!expected[i].expression || covered[i], Error::kOrigin);
  }
  for (std::size_t i = graph.origins.size(); i < tree.origins.size(); ++i) {
    budget.Step();
    RequireMeaning(visited[i] && (std::holds_alternative<CombinedOrigin>(tree.origins[i]) ||
                                  std::holds_alternative<BindingOrigin>(tree.origins[i]) ||
                                  std::holds_alternative<EdgeOrigin>(tree.origins[i]) ||
                                  std::holds_alternative<LoopOrigin>(tree.origins[i]) ||
                                  std::holds_alternative<TryOrigin>(tree.origins[i]) ||
                                  std::holds_alternative<EntryOrigin>(tree.origins[i])), Error::kOrigin);
  }
}
}  // namespace abcd::source
