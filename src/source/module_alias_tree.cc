#include "module_aliases.h"

#include <algorithm>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using Error = ModuleAliasTreeError;
void Require(bool condition, Error error) { if (!condition) throw error; }
}  // namespace

std::variant<std::uint64_t, ModuleAliasTreeError> VerifyModuleAliasTree(
    const ModuleAliases& bindings, const EvaluationPlan& evaluation,
    const FunctionTree& tree, analysis::SemanticBudget budget) {
  try {
    auto work = budget.work;
    const auto step = [&](std::uint64_t count = 1) {
      Require(count <= work, Error::kWorkLimit); work -= count;
    };
    step();
    Require(tree.identity == bindings.identity && tree.method == bindings.method, Error::kIdentity);
    Require(tree.value_revision == bindings.revision && tree.cfg_revision == bindings.cfg_revision &&
        evaluation.revision == bindings.revision && evaluation.cfg_revision == bindings.cfg_revision, Error::kRevision);
    Require(evaluation.modules.size() == bindings.evaluations.size() &&
        tree.modules.size() == bindings.aliases.size() &&
        bindings.names.size() == bindings.aliases.size(), Error::kCoverage);
    for (std::size_t i = 0; i < evaluation.modules.size(); ++i) {
      step();
      const auto& actual = evaluation.modules[i];
      const auto& expected = bindings.evaluations[i];
      Require(actual.pair == expected.pair && actual.read == expected.read &&
          actual.target == expected.target, Error::kTarget);
    }
    Require(tree.module_scope.has_value() == !bindings.aliases.empty(), Error::kCoverage);
    if (tree.module_scope)
      Require(tree.module_scope->value < tree.scopes.size() &&
          tree.scopes[tree.module_scope->value].kind == ScopeKind::kModule, Error::kTarget);
    std::vector<ModuleSymbol> writes;
    Require(bindings.writes.size() <= budget.bytes / sizeof(ModuleSymbol), Error::kResourceLimit);
    writes.reserve(bindings.writes.size());
    Require(writes.capacity() <= budget.bytes / sizeof(ModuleSymbol), Error::kResourceLimit);
    budget.bytes -= writes.capacity() * sizeof(ModuleSymbol);
    std::optional<analysis::OpId> previous_write;
    for (const auto& write : bindings.writes) {
      step();
      Require((!previous_write || *previous_write < write.operation) &&
          write.operation.value < evaluation.operations.size() && !write.binding.initialize &&
          write.binding.target.owner == bindings.owner, Error::kCoverage);
      previous_write = write.operation;
      writes.push_back(write.binding.target);
    }
    std::sort(writes.begin(), writes.end(), [&](auto a, auto b) { step(); return a < b; });
    writes.erase(std::unique(writes.begin(), writes.end(), [&](auto a, auto b) { step(); return a == b; }), writes.end());
    std::size_t next_write = 0;
    std::vector<std::size_t> order;
    Require(tree.modules.size() <= budget.bytes / sizeof(std::size_t), Error::kResourceLimit);
    order.reserve(tree.modules.size());
    Require(order.capacity() <= budget.bytes / sizeof(std::size_t), Error::kResourceLimit);
    for (std::size_t i = 0; i < tree.modules.size(); ++i) { step(); order.push_back(i); }
    std::sort(order.begin(), order.end(), [&](auto a, auto b) {
      step(); return tree.modules[a].target < tree.modules[b].target;
    });
    for (std::size_t i = 0; i < order.size(); ++i) {
      step();
      const auto& expected = bindings.aliases[i];
      const auto& actual = tree.modules[order[i]];
      Require((i == 0 || bindings.aliases[i - 1].target < expected.target) &&
          expected.target.owner == bindings.owner && actual.target == expected.target &&
          actual.symbol.value < tree.symbols.size(), Error::kTarget);
      const auto& symbol = tree.symbols[actual.symbol.value];
      Require(symbol.kind == BindingKind::kModule && symbol.scope == *tree.module_scope, Error::kTarget);
      // Fresh binding evidence grants writes only for proved initialized slots.
      // Child aliases never become initializer-owned declarations.
      Require(next_write == writes.size() || writes[next_write] >= expected.target, Error::kTarget);
      const bool written = next_write < writes.size() && writes[next_write] == expected.target;
      if (written) ++next_write;
      Require(expected.access == (written ? ModuleAccess::kMutable : ModuleAccess::kReadOnly), Error::kAccess);
      Require(!expected.local && !actual.local && actual.access == expected.access &&
          symbol.declaration == (expected.access == ModuleAccess::kMutable ? DeclarationKind::kLet : DeclarationKind::kConst), Error::kAccess);
      Require(expected.name.value >= bindings.first_name.value &&
          expected.name.value - bindings.first_name.value < bindings.names.size() &&
          symbol.name.value < tree.names.size() && symbol.evidence == expected.evidence, Error::kName);
      const auto& name = bindings.names[expected.name.value - bindings.first_name.value];
      const auto& spelling = tree.names[symbol.name.value];
      Require(spelling.size() == name.size(), Error::kName);
      step(name.size()); Require(spelling == name, Error::kName);
    }
    Require(next_write == writes.size(), Error::kCoverage);
    return budget.work - work;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
