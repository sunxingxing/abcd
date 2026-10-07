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
    case TemporaryReason::kExceptionSnapshot: case TemporaryReason::kPendingCompletion:
    case TemporaryReason::kInternalSentinel: case TemporaryReason::kNumericUpdate:
    case TemporaryReason::kDecisionPredicate:
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
  budget.Array(modules_, count);
  if (tree.self) {
    RequireTree(tree.self->value < count &&
        (AllowsNamedSelf(tree.callable) || IsArrowCallable(tree.callable)) &&
        tree.symbols[tree.self->value].kind == BindingKind::kFunctionSelf, Error::kBinding);
    declared_[tree.self->value] = 1;
  }
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
      case BindingKind::kCatchParameter:
        RequireTree(tree.scopes[symbol.scope.value].kind == ScopeKind::kCatch &&
            symbol.declaration == DeclarationKind::kCatchParameter && !symbol.temporary, Error::kBinding);
        break;
      case BindingKind::kLocal:
      case BindingKind::kCompilerTemporary:
        RequireTree(symbol.scope != tree.parameter_scope &&
                    (symbol.declaration == DeclarationKind::kLet || symbol.declaration == DeclarationKind::kConst),
                    Error::kBinding);
        RequireTree(symbol.kind == BindingKind::kCompilerTemporary
            ? symbol.temporary && ValidTemporary(*symbol.temporary) : !symbol.temporary, Error::kBinding);
        break;
      case BindingKind::kFunctionSelf:
        // Self binding is initialized at creation, outside
        // parameters/body. Never turn it into a writable body declaration.
        RequireTree(tree.self == id && tree.scopes[symbol.scope.value].kind == ScopeKind::kFunctionSelf &&
            symbol.declaration == DeclarationKind::kConst && !symbol.temporary, Error::kBinding);
        break;
      case BindingKind::kCapture:
        RequireTree(tree.capture_scope == symbol.scope && symbol.declaration == DeclarationKind::kLet &&
                    !symbol.temporary, Error::kBinding);
        break;
      case BindingKind::kModule:
        RequireTree(tree.module_scope == symbol.scope &&
            (symbol.declaration == DeclarationKind::kLet || symbol.declaration == DeclarationKind::kConst) &&
            !symbol.temporary, Error::kBinding);
        break;
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
  for (std::size_t i = 0; i < tree.parameters.size(); ++i) {
    const auto& parameter = tree.parameters[i];
    budget.Step();
    RequireTree(!parameter.rest || (i + 1 == tree.parameters.size() && !parameter.initializer), Error::kUnsupportedSyntax);
    RequireTree(parameter.symbol.value < count &&
                tree.symbols[parameter.symbol.value].kind == BindingKind::kParameter &&
                !declared_[parameter.symbol.value], Error::kBinding);
    declared_[parameter.symbol.value] = 1;
  }
  for (const auto& capture : tree.captures) {
    budget.Step();
    RequireTree(capture.symbol.value < count && capture.target.owner != tree.owner &&
        tree.symbols[capture.symbol.value].kind == BindingKind::kCapture &&
        !declared_[capture.symbol.value], Error::kBinding);
    declared_[capture.symbol.value] = 1;
  }
  std::vector<ModuleSymbol> targets;
  budget.Array(targets, tree.modules.size());
  for (std::size_t i = 0; i < tree.modules.size(); ++i) {
    budget.Step();
    const auto& alias = tree.modules[i];
    RequireTree(alias.symbol.value < count && !declared_[alias.symbol.value] &&
        tree.symbols[alias.symbol.value].kind == (alias.local ? BindingKind::kLocal : BindingKind::kModule) &&
        (!alias.local || (tree.symbols[alias.symbol.value].scope == tree.body_scope && alias.access == ModuleAccess::kMutable)) &&
        alias.target.owner == tree.modules.front().target.owner, Error::kBinding);
    const auto declaration = tree.symbols[alias.symbol.value].declaration;
    switch (alias.access) {
      case ModuleAccess::kReadOnly: RequireTree(declaration == DeclarationKind::kConst, Error::kBinding); break;
      case ModuleAccess::kMutable: RequireTree(declaration == DeclarationKind::kLet, Error::kBinding); break;
      default: throw Error::kBinding;
    }
    if (!alias.local) declared_[alias.symbol.value] = 1;
    modules_[alias.symbol.value] = alias.target;
    targets[i] = alias.target;
  }
  std::sort(targets.begin(), targets.end(), [&](auto a, auto b) { budget.Step(); return a < b; });
  for (std::size_t i = 1; i < targets.size(); ++i) {
    budget.Step(); RequireTree(targets[i - 1] != targets[i], Error::kBinding);
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
  RequireTree(Resolves(symbol), Error::kScope);
  if (assignment)
    RequireTree(tree_.symbols[symbol.value].declaration != DeclarationKind::kConst, Error::kTarget);
}
bool TreeScopeCheck::Resolves(SymbolId symbol) {
  budget_.Step();
  RequireTree(symbol.value < tree_.symbols.size(), Error::kRange);
  return active_[groups_[symbol.value]] == symbol;
}
void TreeScopeCheck::Declare(SymbolId symbol, ScopeId scope, bool initializer) {
  Reference(symbol);
  RequireTree(tree_.symbols[symbol.value].scope == scope &&
              tree_.symbols[symbol.value].kind != BindingKind::kParameter &&
              tree_.symbols[symbol.value].kind != BindingKind::kCatchParameter &&
              tree_.symbols[symbol.value].kind != BindingKind::kCapture &&
              tree_.symbols[symbol.value].kind != BindingKind::kModule &&
              tree_.symbols[symbol.value].kind != BindingKind::kFunctionSelf &&
              !declared_[symbol.value], Error::kBinding);
  RequireTree(tree_.symbols[symbol.value].declaration != DeclarationKind::kConst || initializer, Error::kBinding);
  declared_[symbol.value] = 1;
}
std::optional<SymbolId> TreeScopeCheck::ActiveBinding(std::u16string_view name) {
  // The traversal activated the exact expression or child-creation scope.
  // Search the shared spelling index, never walk ancestors per free read.
  const auto found = std::lower_bound(ordered_.begin(), ordered_.end(), name, [&](SymbolId id, auto wanted) {
    const auto& actual = tree_.names[tree_.symbols[id.value].name.value];
    budget_.Step(std::min(actual.size(), wanted.size()) + 1); return actual < wanted;
  });
  if (found == ordered_.end()) return {};
  const auto& actual = tree_.names[tree_.symbols[found->value].name.value];
  budget_.Step(std::min(actual.size(), name.size()) + 1);
  if (actual != name) return {};
  return active_[groups_[found->value]];
}
void TreeScopeCheck::EnclosingReference(std::u16string_view name, const ModuleSymbol* module) {
  const auto active = ActiveBinding(name);
  RequireTree(!active || (module && modules_[active->value] && *modules_[active->value] == *module),
      TreeStructureError::kBinding);
}
void TreeScopeCheck::FreeReference(std::u16string_view name) {
  RequireTree(!ActiveBinding(name), TreeStructureError::kGlobalShadowed);
}
void TreeScopeCheck::CatchParameter(SymbolId symbol, ScopeId scope) {
  RequireTree(symbol.value < tree_.symbols.size(), Error::kRange);
  RequireTree(tree_.symbols[symbol.value].scope == scope &&
      tree_.symbols[symbol.value].kind == BindingKind::kCatchParameter &&
      !declared_[symbol.value], Error::kBinding);
  Reference(symbol);
  declared_[symbol.value] = 1;
}
void TreeScopeCheck::Finish() {
  for (const auto declaration : declared_) { budget_.Step(); RequireTree(declaration, Error::kBinding); }
  for (const auto scope : entered_) { budget_.Step(); RequireTree(scope, Error::kScope); }
  for (const auto active : active_) { budget_.Step(); RequireTree(!active, Error::kScope); }
}
std::vector<std::uint8_t> ResolveTreeReferences(const FunctionTree& tree,
    std::span<const TreeScopedReference> references, TreeCheckBudget& budget) {
  TreeScopeCheck scopes(tree,budget);
  std::vector<std::uint8_t> resolved;
  std::vector<std::optional<ScopeId>> heads,next;
  std::vector<std::optional<std::size_t>> queries,following;
  struct Visit { ScopeId scope; std::optional<ScopeId> child; bool entered = false; };
  std::vector<Visit> stack;
  budget.Array(resolved,references.size());budget.Array(heads,tree.scopes.size());
  budget.Array(next,tree.scopes.size());budget.Array(queries,tree.scopes.size());
  budget.Array(following,references.size());budget.Array(stack,tree.scopes.size());
  for (std::size_t i=0;i<tree.scopes.size();++i) {
    budget.Step();if(const auto parent=tree.scopes[i].parent) {
      RequireTree(parent->value<tree.scopes.size(),Error::kScope);
      next[i]=heads[parent->value];heads[parent->value]=ScopeId{static_cast<std::uint32_t>(i)};
    }
  }
  for (std::size_t i=0;i<references.size();++i) {
    budget.Step();const auto scope=references[i].scope;
    RequireTree(scope.value<tree.scopes.size(),Error::kScope);
    following[i]=queries[scope.value];queries[scope.value]=i;
  }
  for (std::size_t i=0;i<tree.scopes.size();++i) {
    budget.Step();if(tree.scopes[i].parent)continue;
    std::size_t pending=1;stack[0]={ScopeId{static_cast<std::uint32_t>(i)},{}};
    while(pending) {
      budget.Step();auto& visit=stack[pending-1];
      if(!visit.entered) {
        scopes.Enter(visit.scope);visit.entered=true;visit.child=heads[visit.scope.value];
        for(auto query=queries[visit.scope.value];query;query=following[*query]) {
          budget.Step();resolved[*query]=scopes.Resolves(references[*query].symbol);
        }
      }
      if(const auto child=visit.child) {
        visit.child=next[child->value];RequireTree(pending<stack.size(),Error::kScope);
        stack[pending++]={*child,{}};
      } else {scopes.Leave(visit.scope);--pending;}
    }
  }
  return resolved;
}
}  // namespace abcd::source
