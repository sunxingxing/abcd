#include "environment_renewal_binding.h"

#include <bit>
#include <tuple>

#include "original_flow.h"
namespace abcd::source {
const analysis::EnvironmentReset* RenewalDeclaration(
    const analysis::ValueProgram& graph, const AllocatedCell& cell,
    analysis::OpId store) {
  const auto& owner = graph.environment_renewal;
  if (!owner || !owner->flow || store.value >= graph.ops.size() ||
      cell.allocation.value >= graph.ops.size())
    return nullptr;
  const auto& proof = owner->proof;
  const auto site =
      graph.origins[graph.ops[store.value].origin.value].site.value;
  if (site >= proof.initializers.size() || !proof.initializers[site])
    return nullptr;
  const auto& reset = proof.resets[*proof.initializers[site]];
  if (reset.family.value >= proof.allocations.size() ||
      cell.slot.value != reset.slot)
    return nullptr;
  const auto allocation =
      owner->flow->nodes[proof.allocations[reset.family.value].instruction]
          .instruction.value;
  if (graph.origins[graph.ops[cell.allocation.value].origin.value].site.value !=
      allocation)
    return nullptr;
  return &reset;
}
void VerifyRenewalInput(const analysis::ValueProgram& graph,
                        TreeCheckBudget& budget) {
  if (!graph.environment_renewal) return;
  const auto checked = analysis::VerifyEnvironmentRenewalEvidence(
      *graph.environment_renewal, budget.Remaining());
  if (const auto* failure =
          std::get_if<analysis::EnvironmentRenewalFailure>(&checked))
    throw failure->kind == analysis::EnvironmentRenewalError::kResourceLimit
        ? TreeStructureError::kResourceLimit
    : failure->kind == analysis::EnvironmentRenewalError::kWorkLimit
        ? TreeStructureError::kWorkLimit
        : TreeStructureError::kOwnership;
  budget.Step(std::get<std::uint64_t>(checked));
}
}  // namespace abcd::source

namespace abcd::source {
void VerifyRenewalBindings(const FunctionTree& tree,
                           const OriginalFlow& original,
                           TreeCheckBudget& budget) {
  if (!original.environment_renewal) return;
  const auto& owner = *original.environment_renewal;
  const auto checked =
      analysis::VerifyEnvironmentRenewalEvidence(owner, budget.Remaining());
  if (const auto* failure =
          std::get_if<analysis::EnvironmentRenewalFailure>(&checked))
    throw failure->kind == analysis::EnvironmentRenewalError::kResourceLimit
        ? TreeStructureError::kResourceLimit
    : failure->kind == analysis::EnvironmentRenewalError::kWorkLimit
        ? TreeStructureError::kWorkLimit
        : TreeStructureError::kOwnership;
  budget.Step(std::get<std::uint64_t>(checked));
  RequireTree(original.cells.has_value(), TreeStructureError::kOwnership);
  std::vector<std::uint32_t> loop_depth;
  budget.Array(loop_depth, tree.scopes.size());
  for (const auto& statement : tree.statements) {
    budget.Step();
    std::optional<StmtId> body;
    if (const auto* l = std::get_if<LoopStmt>(&statement.statement))
      body = l->body;
    if (const auto* l = std::get_if<ForOfStmt>(&statement.statement))
      body = l->body;
    if (const auto* l = std::get_if<ForInStmt>(&statement.statement))
      body = l->body;
    if (body) {
      const auto* block =
          std::get_if<BlockStmt>(&tree.statements[body->value].statement);
      RequireTree(block, TreeStructureError::kOwnership);
      loop_depth[block->scope.value] = 1;
    }
  }
  std::vector<std::uint32_t> heads, next, pending;
  budget.Array(heads, tree.scopes.size());
  budget.Array(next, tree.scopes.size());
  budget.Array(pending, tree.scopes.size());
  std::fill(heads.begin(), heads.end(), UINT32_MAX);
  std::fill(next.begin(), next.end(), UINT32_MAX);
  std::size_t count = 0;
  for (std::uint32_t s = 0; s < tree.scopes.size(); ++s) {
    budget.Step();
    if (const auto parent = tree.scopes[s].parent) {
      RequireTree(parent->value < tree.scopes.size(),
                  TreeStructureError::kOwnership);
      next[s] = heads[parent->value];
      heads[parent->value] = s;
    } else
      pending[count++] = s;
  }
  for (std::size_t cursor = 0; cursor < count; ++cursor) {
    const auto s = pending[cursor];
    for (auto child = heads[s]; child != UINT32_MAX; child = next[child]) {
      budget.Step();
      RequireTree(count < pending.size(), TreeStructureError::kOwnership);
      loop_depth[child] += loop_depth[s];
      pending[count++] = child;
    }
  }
  RequireTree(count == tree.scopes.size(), TreeStructureError::kOwnership);
  using Key = std::tuple<std::uint32_t, std::uint16_t, std::uint32_t>;
  std::vector<Key> cells;
  budget.Admit(original.cells->bindings.size(), sizeof(Key));
  cells.reserve(original.cells->bindings.size());
  budget.Admit(cells.capacity() - original.cells->bindings.size(), sizeof(Key));
  for (std::uint32_t c = 0; c < original.cells->bindings.size(); ++c) {
    const auto& b = original.cells->bindings[c];
    if (const auto* e =
            std::get_if<analysis::EnvironmentId>(&b.storage.environment))
      cells.emplace_back(e->value, b.storage.slot.value, c);
  }
  std::sort(cells.begin(), cells.end(), [&](auto a, auto b) {
    budget.Step();
    return a < b;
  });
  for (const auto& reset : owner.proof.resets) {
    budget.Step(1 + std::bit_width(cells.size()));
    const auto it = std::lower_bound(cells.begin(), cells.end(),
                                     Key{reset.family.value, reset.slot, 0});
    RequireTree(it != cells.end() && std::get<0>(*it) == reset.family.value &&
                    std::get<1>(*it) == reset.slot,
                TreeStructureError::kOwnership);
    const auto& cell = original.cells->bindings[std::get<2>(*it)];
    RequireTree(cell.declaration && cell.scope.value < loop_depth.size() &&
                    loop_depth[cell.scope.value] > 0,
                TreeStructureError::kOwnership);
    const auto& d = tree.statements[cell.declaration->value];
    const auto* declaration = std::get_if<DeclarationStmt>(&d.statement);
    RequireTree(declaration && declaration->symbol == cell.binding &&
                    declaration->initializer &&
                    tree.symbols[cell.binding.value].scope == cell.scope &&
                    tree.symbols[cell.binding.value].declaration ==
                        DeclarationKind::kLet,
                TreeStructureError::kOwnership);
    // The original initialization operation, not an allocation anchor or a
    // later copy, owns this declaration's successful binding Commit.
    const auto instruction =
        owner.flow->nodes[reset.initializer.value].instruction.value;
    RequireTree(
        instruction < original.operations.size() &&
            original.operations[instruction].statement == cell.declaration,
        TreeStructureError::kOwnership);
  }
}
}  // namespace abcd::source
