#include "tree_correspondence.h"

namespace abcd::source {
namespace {
using Error = TreeSemanticError;
template <class Origin>
bool SameSite(const Origin& tree, const analysis::ValueOrigin& input) {
  return tree.site == input.site && tree.pc == input.pc && tree.original == input.original;
}
}  // namespace

void CheckTreeCorrespondenceOrigins(const analysis::ValueProgram& graph,
    const FunctionTree& tree, const TreeFlow& flow, TreeCheckBudget& budget) {
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
  std::vector<std::optional<ExprId>> expected;
  std::vector<std::uint8_t> covered;
  std::vector<std::uint8_t> visited;
  std::vector<TreeOriginId> stack;
  std::vector<std::uint8_t> edge_expressions;
  budget.Admit(1, sizeof(expected) + sizeof(covered) + sizeof(visited) + sizeof(stack) + sizeof(edge_expressions));
  budget.Array(expected, graph.origins.size());
  budget.Array(covered, graph.origins.size());
  budget.Array(visited, tree.origins.size());
  budget.Array(stack, tree.origin_parts.size() + 1);
  for (const auto origin : flow.controls) {
    budget.Step();
    RequireMeaning(origin.value >= graph.origins.size() && origin.value < tree.origins.size() &&
        !visited[origin.value] &&
        (std::holds_alternative<LoopOrigin>(tree.origins[origin.value]) ||
         std::holds_alternative<EdgeOrigin>(tree.origins[origin.value])), Error::kOrigin);
    visited[origin.value] = 1;
  }
  if (!flow.copies.empty()) budget.Array(edge_expressions, tree.expressions.size());
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
      const auto& assignment = std::get<AssignmentExpr>(node.expression);
      // Flow checked the statement and RHS origins; operand checking proved
      // the RHS is a leaf. No original-PC provenance is invented for copies.
      RequireMeaning(!edge_expressions[root.value] && !edge_expressions[assignment.value.value], Error::kOrigin);
      edge_expressions[root.value] = 1;
      edge_expressions[assignment.value.value] = 1;
    }
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    const auto& op = graph.ops[i];
    if (op.life != analysis::ValueLife::kLive) continue;
    RequireMeaning(op.origin.value < graph.origins.size(), Error::kOrigin);
    const TreeOriginId origin{op.origin.value};
    if (flow.statements[i])
      RequireMeaning(tree.statements[flow.statements[i]->value].origin == origin, Error::kOrigin);
    if (flow.snapshots[i]) {
      const auto& statement = std::get<ExpressionStmt>(tree.statements[flow.statements[i]->value].statement);
      RequireMeaning(tree.expressions[statement.expression.value].origin == origin, Error::kOrigin);
    }
    if (std::holds_alternative<analysis::ValueJump>(op.operation) ||
        std::holds_alternative<analysis::ValueReturn>(op.operation)) continue;
    RequireMeaning(flow.expressions[i].has_value(), Error::kOrigin);
    auto& owner = expected[op.origin.value];
    RequireMeaning(!owner || owner == flow.expressions[i], Error::kOrigin);
    owner = flow.expressions[i];
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
      const auto* origin = std::get_if<BindingOrigin>(&tree.origins[statement.origin.value]);
      RequireMeaning(origin && origin->symbol == declaration->symbol && !visited[statement.origin.value], Error::kOrigin);
      visited[statement.origin.value] = 1;
    }
  }
  for (std::size_t i = 0; i < tree.expressions.size(); ++i) {
    budget.Step();
    const ExprId expression{static_cast<std::uint32_t>(i)};
    const auto origin = tree.expressions[i].origin;
    if (std::holds_alternative<EdgeOrigin>(tree.origins[origin.value]) ||
        std::holds_alternative<EntryOrigin>(tree.origins[origin.value])) {
      RequireMeaning(!edge_expressions.empty() && edge_expressions[i], Error::kOrigin);
      visited[origin.value] = 1;
      continue;
    }
    if (origin.value < graph.origins.size()) {
      if (expected[origin.value] == expression) covered[origin.value] = 1;
      continue;
    }
    std::size_t pending = 1;
    stack[0] = origin;
    while (pending != 0) {
      budget.Step();
      const auto part = stack[--pending];
      if (part.value < graph.origins.size()) {
        RequireMeaning(expected[part.value] == expression, Error::kOrigin);
        covered[part.value] = 1;
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
    RequireMeaning(!expected[i] || covered[i], Error::kOrigin);
  }
  for (std::size_t i = graph.origins.size(); i < tree.origins.size(); ++i) {
    budget.Step();
    RequireMeaning(visited[i] && (std::holds_alternative<CombinedOrigin>(tree.origins[i]) ||
                                  std::holds_alternative<BindingOrigin>(tree.origins[i]) ||
                                  std::holds_alternative<EdgeOrigin>(tree.origins[i]) ||
                                  std::holds_alternative<LoopOrigin>(tree.origins[i]) ||
                                  std::holds_alternative<EntryOrigin>(tree.origins[i])), Error::kOrigin);
  }
}
}  // namespace abcd::source
