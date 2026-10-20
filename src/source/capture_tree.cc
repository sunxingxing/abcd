#include "capture_tree.h"
#include "class_tree.h"
#include "class_inner_scope.h"
#include <bit>
#include "function_context.h"
#include <algorithm>
#include <bit>

namespace abcd::source {
namespace {
using Error = CaptureTreeError;
void Require(bool condition, Error error) { if (!condition) throw error; }
void Revision(CaptureTree function, analysis::IrRevision value, analysis::CfgRevision cfg) {
  Require(function.tree.value_revision == value && function.tree.cfg_revision == cfg &&
      function.flow.value_revision == value && function.flow.cfg_revision == cfg, Error::kRevision);
}
SymbolId CellSymbol(CaptureTree function, CellId cell) {
  Require(cell.value < function.flow.cells.size() && function.flow.cells[cell.value], Error::kCell);
  const auto symbol = *function.flow.cells[cell.value];
  Require(symbol.value < function.tree.symbols.size(), Error::kCell);
  return symbol;
}
}  // namespace

static std::span<const OriginalCellBinding> CellBindings(const TreeFlow& flow) {
  if (flow.original && flow.original->cells) return flow.original->cells->bindings;
  return flow.cell_bindings;
}
bool HasDeferredDeclaration(CaptureTree parent, CellId cell) {
  if (cell.value >= CellBindings(parent.flow).size() || cell.value >= parent.flow.cells.size() ||
      !parent.flow.cells[cell.value]) return false;
  const auto& binding = CellBindings(parent.flow)[cell.value];
  if (binding.activation || !binding.declaration || binding.binding != *parent.flow.cells[cell.value] ||
      binding.declaration->value >= parent.tree.statements.size() || binding.binding.value >= parent.tree.symbols.size()) return false;
  const auto& symbol = parent.tree.symbols[binding.binding.value];
  const auto* declaration = std::get_if<DeclarationStmt>(&parent.tree.statements[binding.declaration->value].statement);
  return declaration && declaration->initializer && declaration->symbol == binding.binding &&
      symbol.kind == BindingKind::kLocal && symbol.declaration == DeclarationKind::kLet &&
      !symbol.temporary && symbol.scope == binding.scope;
}
std::variant<std::uint64_t, CaptureTreeError> VerifyDeferredCaptureTree(
    const CaptureCells& cells, CaptureTree parent, analysis::SemanticBudget budget, std::optional<std::size_t>* rejected) {
  if (rejected) rejected->reset();
  try {
    Require(parent.tree.identity == cells.identity && parent.tree.method == cells.parent, Error::kOwner);
    Revision(parent, cells.parent_revision, cells.parent_cfg);
    auto work = budget.work;
    for (std::size_t i = 0; i < cells.deferred.size(); ++i) {
      if (rejected) *rejected = i;
      Require(work != 0, Error::kWorkLimit); --work;
      const auto& entry = cells.deferred[i];
      Require(HasDeferredDeclaration(parent, entry.cell), Error::kCell);
      const auto& binding = CellBindings(parent.flow)[entry.cell.value];
      Require(entry.store.value < parent.flow.statements.size() &&
          parent.flow.statements[entry.store.value] == binding.declaration &&
          entry.store.value < parent.flow.expressions.size(), Error::kTarget);
      const auto& declaration = std::get<DeclarationStmt>(parent.tree.statements[binding.declaration->value].statement);
      Require(parent.flow.expressions[entry.store.value] == declaration.initializer, Error::kTarget);
      // TreeFlow independently checked this original store/declaration against
      // the creator IR. Operand/recovery evidence is attached by the enclosing
      // semantics pass; a construction-only flow need not own that later product.
      if (parent.flow.original && parent.flow.original->cells) {
        Require(entry.cell.value < parent.flow.original->cells->bindings.size(), Error::kCell);
        const auto& original = parent.flow.original->cells->bindings[entry.cell.value];
        Require(original.storage.environment == binding.storage.environment &&
            original.storage.slot.value == binding.storage.slot.value && original.binding == binding.binding &&
            original.scope == binding.scope && original.declaration == binding.declaration &&
            original.activation == binding.activation, Error::kTarget);
      }
    }
    if (rejected) rejected->reset();
    return budget.work - work;
  } catch (Error error) { return error; }
}

std::variant<std::uint64_t, CaptureTreeError> VerifyCaptureTree(
    const CaptureCells& cells, CaptureTree parent, CaptureTree child, analysis::SemanticBudget budget,
    const ClassCaptureAnchor* class_anchor) {
  try {
    auto work = budget.work;
    const auto step = [&](std::uint64_t count = 1) {
      Require(count <= work, Error::kWorkLimit); work -= count;
    };
    step();
    const auto& outer = parent.tree;
    const auto& inner = child.tree;
    Require(outer.owner != inner.owner && outer.identity == cells.identity && inner.identity == cells.identity &&
        outer.method == cells.parent && inner.method == cells.child, Error::kOwner);
    Revision(parent, cells.parent_revision, cells.parent_cfg);
    Revision(child, cells.child_revision, cells.child_cfg);
    CaptureRange range{};
    if (class_anchor) {
      Require(inner.callable == class_anchor->owner.Callable(), Error::kOwner);
      const auto checked = VerifyClassCaptureAnchor(cells, parent, *class_anchor, {budget.bytes, work});
      if (const auto* error = std::get_if<Error>(&checked)) throw *error;
      step(std::get<std::uint64_t>(checked));
      const auto deferred = VerifyDeferredCaptureTree(cells, parent, {budget.bytes, work});
      if (const auto* error = std::get_if<Error>(&deferred)) throw *error;
      step(std::get<std::uint64_t>(deferred));
    } else {
      Require(cells.creation.value < parent.flow.expressions.size() &&
          parent.flow.expressions[cells.creation.value], Error::kCreation);
      const auto expression = *parent.flow.expressions[cells.creation.value];
      Require(expression.value < outer.expressions.size(), Error::kCreation);
      const auto* creation = std::get_if<FunctionExpr>(&outer.expressions[expression.value].expression);
      Require(creation && creation->child == inner.owner, Error::kCreation);
      range = creation->captures;
      Require(range.first <= outer.closure_captures.size() &&
          range.count <= outer.closure_captures.size() - range.first && range.count == cells.targets.size(), Error::kCoverage);
    }
    Require(inner.captures.size() == cells.targets.size(), Error::kCoverage);
    for (std::size_t i = 0; i < cells.targets.size(); ++i) {
      step();
      const auto& binding = cells.targets[i];
      const auto* target = std::get_if<ParentCell>(&binding.target);
      Require(target != nullptr, Error::kOuterCapture);
      const auto parent_symbol = CellSymbol(parent, target->cell);
      const auto child_symbol = CellSymbol(child, binding.child);
      const auto& alias = inner.captures[i];
      Require((class_anchor || outer.closure_captures[range.first + i] == parent_symbol) && alias.symbol == child_symbol &&
          alias.target == CapturedSymbol{outer.owner, parent_symbol}, Error::kTarget);
      const auto& source = outer.symbols[parent_symbol.value];
      const auto& destination = inner.symbols[child_symbol.value];
      if (destination.kind == BindingKind::kClassInner) {
        Require(inner.class_scope && inner.class_inner && child.flow.original &&
            child.flow.original->cells && binding.child.value < child.flow.original->cells->bindings.size(), Error::kCell);
        const auto& storage = child.flow.original->cells->bindings[binding.child.value].storage;
        const auto* depth = std::get_if<analysis::CapturedDepth>(&storage.environment);
        step(std::bit_width(inner.class_scope->Scope().requirements.size()) + 1);
        Require(depth && inner.class_scope->SelfStorage({*depth, storage.slot}), Error::kCell);
        if (class_anchor)
          Require(inner.class_scope->Creator() == outer.method &&
              inner.class_scope->Scope().binding.creation == class_anchor->creation,
              Error::kCreation);
        else Require(source.kind == BindingKind::kClassInner && outer.class_scope &&
            outer.class_scope->SameClass(*inner.class_scope), Error::kCell);
        // The alias above accounts for ORIGINAL environment transport. The
        // certified inner symbol is a native class binding, not this cache.
        continue;
      }
      Require((source.kind == BindingKind::kLocal || source.kind == BindingKind::kCapture) &&
          destination.kind == BindingKind::kCapture, Error::kCell);
      Require(source.name.value < outer.names.size() && destination.name.value < inner.names.size(), Error::kName);
      const auto& source_name = outer.names[source.name.value];
      const auto& destination_name = inner.names[destination.name.value];
      Require(source_name.size() == destination_name.size(), Error::kName);
      step(source_name.size());
      Require(source_name == destination_name, Error::kName);
    }
    return budget.work - work;
  } catch (Error error) { return error; }
}
static std::variant<std::uint64_t, CaptureTreeError> CheckClassSyntax(
    const CaptureCells& cells, ClassCaptureDestination parent, const FunctionContext& owner,
    analysis::SemanticBudget budget) {
  try {
    auto work = budget.work;
    const auto step = [&](std::uint64_t count = 1) { Require(count <= work, Error::kWorkLimit); work -= count; };
    step();
    Require(owner.ClassContext() && owner.Identity() == cells.identity && owner.Target() == cells.child &&
        parent.tree.identity == cells.identity && parent.tree.method == cells.parent, Error::kOwner);
    Require(parent.tree.value_revision == cells.parent_revision && parent.tree.cfg_revision == cells.parent_cfg, Error::kRevision);
    const auto expression = parent.expression;
    Require(expression.value < parent.tree.expressions.size(), Error::kCreation);
    const auto* syntax = std::get_if<ClassExpr>(&parent.tree.expressions[expression.value].expression);
    Require(syntax && syntax->definition, Error::kCreation);
    const auto& layout = *syntax->definition;
    const auto creation = layout.operation;
    Require(layout.identity == cells.identity && layout.creation.creator == cells.parent, Error::kOwner);
    if (const auto* constructor = owner.ConstructorSite()) {
      Require(!owner.ClassContext()->Occurrence() && cells.creation == creation &&
          cells.input.pc == constructor->pc && cells.input.target == owner.Position() &&
          layout.creation.target == constructor->target && layout.creation.creator == constructor->creator &&
          layout.creation.pc == constructor->pc && layout.creation.creation == constructor->creation, Error::kCreation);
    } else if (const auto* member = owner.ClassSite()) {
      const auto occurrence = owner.ClassContext()->Occurrence();
      Require(occurrence && member->property < layout.member_sites.size() && layout.member_sites[member->property] == *occurrence &&
          cells.creation == creation && cells.input.pc == member->pc && cells.input.target == owner.Position() &&
          layout.creation.creator == member->creator && layout.creation.pc == member->pc &&
          layout.creation.creation == member->creation, Error::kCreation);
    } else {
      const auto* defined = owner.ClassContext()->DefinedMember();
      Require(defined && !owner.ClassContext()->Occurrence() && defined->function.creator == cells.parent &&
          cells.input.pc == defined->function.pc && cells.input.target == owner.Position(), Error::kCreation);
      step(std::bit_width(layout.definitions.size()) + 1);
      const auto found = std::lower_bound(layout.definitions.begin(), layout.definitions.end(), defined->definition,
          [](const auto& definition, auto wanted) { return definition.site.definition < wanted; });
      Require(found != layout.definitions.end() && found->site.definition == defined->definition &&
          found->site.creation == creation && found->site.Method(defined->kind) == cells.creation &&
          defined->placement == (found->site.prototype ? abc::ClassPlacement::kPrototype : abc::ClassPlacement::kStatic), Error::kCreation);
    }
    return budget.work - work;
  } catch (Error error) { return error; }
}
std::variant<std::uint64_t, CaptureTreeError> VerifyClassCaptureAnchor(
    const CaptureCells& cells, CaptureTree parent, const ClassCaptureAnchor& anchor,
    analysis::SemanticBudget budget) {
  try {
    Revision(parent, cells.parent_revision, cells.parent_cfg);
    Require(anchor.creation.value < parent.flow.expressions.size() && parent.flow.expressions[anchor.creation.value], Error::kCreation);
    const auto expression = *parent.flow.expressions[anchor.creation.value];
    Require(expression.value < parent.tree.expressions.size(), Error::kCreation);
    const auto* syntax = std::get_if<ClassExpr>(&parent.tree.expressions[expression.value].expression);
    Require(syntax && syntax->definition && syntax->definition->operation == anchor.creation, Error::kCreation);
    return CheckClassSyntax(cells, {parent.tree, expression}, anchor.owner, budget);
  } catch (Error error) { return error; }
}
std::variant<std::uint64_t, CaptureTreeError> VerifyClassCaptureDestination(
    const CaptureCells& cells, const FunctionTree& child, ClassCaptureDestination parent,
    const FunctionContext& owner, analysis::SemanticBudget budget) {
  try {
    const auto anchor = CheckClassSyntax(cells, parent, owner, budget);
    if (const auto* error = std::get_if<Error>(&anchor)) return *error;
    auto work = budget.work - std::get<std::uint64_t>(anchor);
    const auto step = [&](std::uint64_t count = 1) { Require(count <= work, Error::kWorkLimit); work -= count; };
    Require(child.identity == cells.identity && child.method == cells.child && child.owner != parent.tree.owner &&
        child.callable == owner.Callable(), Error::kOwner);
    Require(child.value_revision == cells.child_revision && child.cfg_revision == cells.child_cfg, Error::kRevision);
    Require(child.captures.size() == cells.targets.size(), Error::kCoverage);
    for (const auto& alias : child.captures) {
      step();
      Require(alias.target.owner == parent.tree.owner && alias.target.symbol.value < parent.tree.symbols.size() &&
          alias.symbol.value < child.symbols.size(), Error::kTarget);
      const auto& source = parent.tree.symbols[alias.target.symbol.value];
      const auto& target = child.symbols[alias.symbol.value];
      if (target.kind == BindingKind::kClassInner) {
        Require(child.class_scope && child.class_inner &&
            child.class_scope->Creator() == parent.tree.method &&
            parent.expression.value < parent.tree.expressions.size() &&
            std::get<ClassExpr>(parent.tree.expressions[parent.expression.value].expression).definition->class_self &&
            std::get<ClassExpr>(parent.tree.expressions[parent.expression.value].expression).definition->class_self->certificate &&
            child.class_scope->SameClass(*std::get<ClassExpr>(parent.tree.expressions[parent.expression.value].expression).definition->class_self->certificate), Error::kCell);
        const auto& spelling = std::get<ClassExpr>(parent.tree.expressions[parent.expression.value].expression).definition->name;
        Require(target.name.value < child.names.size(), Error::kName);
        step(spelling.size());
        Require(child.names[target.name.value] == spelling, Error::kName);
        continue;
      }
      Require((source.kind == BindingKind::kLocal || source.kind == BindingKind::kCapture) &&
          target.kind == BindingKind::kCapture, Error::kCell);
      Require(source.name.value < parent.tree.names.size() && target.name.value < child.names.size(), Error::kName);
      const auto& name = parent.tree.names[source.name.value];
      step(name.size()); Require(name == child.names[target.name.value], Error::kName);
    }
    return budget.work - work;
  } catch (Error error) { return error; }
}
}  // namespace abcd::source
