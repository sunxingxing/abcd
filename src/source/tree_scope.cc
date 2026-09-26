#include "tree_checks.h"

#include <algorithm>

#include "abcd/source/scalar_expression.h"

namespace abcd::source {
namespace {
using Error = TreeStructureError;
bool ValidEvidence(NameEvidence evidence) {
  switch (evidence) {
    case NameEvidence::kMetadata: case NameEvidence::kDebugRange:
    case NameEvidence::kSynthesized: return true;
  }
  return false;
}
bool ValidTemporary(TemporaryReason reason) {
  switch (reason) {
    case TemporaryReason::kMultipleUses: case TemporaryReason::kEffectSnapshot:
    case TemporaryReason::kPhiJoin: case TemporaryReason::kParallelCopyCycle:
    case TemporaryReason::kCrossRegionValue: case TemporaryReason::kReferenceSnapshot:
    case TemporaryReason::kUnprovedInline: case TemporaryReason::kPredicateSnapshot:
      return true;
  }
  return false;
}
}  // namespace

TreeScopeCheck::TreeScopeCheck(const FunctionTree& tree, TreeCheckBudget& budget)
    : tree_(tree), budget_(budget) {
  const auto count = tree.symbols.size();
  budget.Array(ordered_, count);
  budget.Array(groups_, count);
  budget.Array(heads_, tree.scopes.size());
  budget.Array(next_, count);
  budget.Array(active_, count);
  budget.Array(previous_, count);
  budget.Array(declared_, count);
  budget.Array(entered_, tree.scopes.size());
  RequireTree(!tree.self, Error::kUnsupportedSyntax);
  for (std::size_t i = 0; i < count; ++i) {
    budget.Step();
    const SymbolId id{static_cast<std::uint32_t>(i)};
    ordered_[i] = id;
    const auto& symbol = tree.symbols[i];
    RequireTree(symbol.scope.value < tree.scopes.size() && symbol.name.value < tree.names.size(), Error::kRange);
    RequireTree(ValidEvidence(symbol.evidence), Error::kName);
    budget.Step(tree.names[symbol.name.value].size());
    RequireTree(IsSourceIdentifier(std::u16string_view{tree.names[symbol.name.value]}), Error::kName);
    next_[i] = heads_[symbol.scope.value];
    heads_[symbol.scope.value] = id;
    switch (symbol.kind) {
      case BindingKind::kParameter:
        RequireTree(symbol.scope == tree.parameter_scope &&
                    symbol.declaration == DeclarationKind::kParameter && !symbol.temporary, Error::kBinding);
        break;
      case BindingKind::kLocal:
      case BindingKind::kCompilerTemporary:
        RequireTree(symbol.scope != tree.parameter_scope &&
                    (symbol.declaration == DeclarationKind::kLet || symbol.declaration == DeclarationKind::kConst),
                    Error::kBinding);
        RequireTree(symbol.kind == BindingKind::kCompilerTemporary
            ? symbol.temporary && ValidTemporary(*symbol.temporary) : !symbol.temporary, Error::kBinding);
        break;
      case BindingKind::kFunctionSelf: throw Error::kUnsupportedSyntax;
      default: throw Error::kBinding;
    }
  }
  const auto compare_names = [&](SymbolId left, SymbolId right) {
    const auto& a = tree.names[tree.symbols[left.value].name.value];
    const auto& b = tree.names[tree.symbols[right.value].name.value];
    budget.Step(std::min(a.size(), b.size()) + 1);
    return a.compare(b);
  };
  std::sort(ordered_.begin(), ordered_.end(), [&](SymbolId left, SymbolId right) {
    const auto names = compare_names(left, right);
    return names != 0 ? names < 0 : tree.symbols[left.value].scope < tree.symbols[right.value].scope;
  });
  std::size_t group = 0;
  for (std::size_t i = 0; i < ordered_.size(); ++i) {
    budget.Step();
    if (i != 0) {
      if (compare_names(ordered_[i - 1], ordered_[i]) != 0) ++group;
      else RequireTree(tree.symbols[ordered_[i - 1].value].scope != tree.symbols[ordered_[i].value].scope,
                       Error::kName);
    }
    groups_[ordered_[i].value] = group;
  }
  for (const auto& parameter : tree.parameters) {
    budget.Step();
    RequireTree(!parameter.rest, Error::kUnsupportedSyntax);
    RequireTree(parameter.symbol.value < count &&
                tree.symbols[parameter.symbol.value].kind == BindingKind::kParameter &&
                !declared_[parameter.symbol.value], Error::kBinding);
    declared_[parameter.symbol.value] = 1;
  }
}

void TreeScopeCheck::Enter(ScopeId scope) {
  budget_.Step();
  RequireTree(scope.value < entered_.size() && !entered_[scope.value], Error::kScope);
  entered_[scope.value] = 1;
  auto symbol = heads_[scope.value];
  while (symbol) {
    budget_.Step();
    auto& active = active_[groups_[symbol->value]];
    previous_[symbol->value] = active;
    // A body lexical declaration cannot redeclare a simple formal even though
    // the syntax model gives parameter/body environments distinct ScopeIds.
    RequireTree(scope != tree_.body_scope || !active ||
                tree_.symbols[active->value].kind != BindingKind::kParameter, Error::kName);
    active = *symbol;
    symbol = next_[symbol->value];
  }
}
void TreeScopeCheck::Leave(ScopeId scope) {
  auto symbol = heads_[scope.value];
  while (symbol) {
    budget_.Step();
    auto& active = active_[groups_[symbol->value]];
    RequireTree(active == symbol, Error::kScope);
    active = previous_[symbol->value];
    symbol = next_[symbol->value];
  }
}
void TreeScopeCheck::Reference(SymbolId symbol, bool assignment) {
  budget_.Step();
  RequireTree(symbol.value < tree_.symbols.size(), Error::kRange);
  RequireTree(active_[groups_[symbol.value]] == symbol, Error::kScope);
  if (assignment)
    RequireTree(tree_.symbols[symbol.value].declaration != DeclarationKind::kConst, Error::kTarget);
}
void TreeScopeCheck::Declare(SymbolId symbol, ScopeId scope, bool initializer) {
  Reference(symbol);
  RequireTree(tree_.symbols[symbol.value].scope == scope &&
              tree_.symbols[symbol.value].kind != BindingKind::kParameter &&
              !declared_[symbol.value], Error::kBinding);
  RequireTree(tree_.symbols[symbol.value].declaration != DeclarationKind::kConst || initializer, Error::kBinding);
  declared_[symbol.value] = 1;
}
void TreeScopeCheck::Finish() {
  for (const auto declaration : declared_) { budget_.Step(); RequireTree(declaration, Error::kBinding); }
  for (const auto scope : entered_) { budget_.Step(); RequireTree(scope, Error::kScope); }
  for (const auto active : active_) { budget_.Step(); RequireTree(!active, Error::kScope); }
}
}  // namespace abcd::source
