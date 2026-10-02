#include "capture_tree.h"
#include "class_tree.h"
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
std::variant<std::uint64_t, CaptureTreeError> VerifyClassCaptureAnchor(
    const CaptureCells& cells, CaptureTree parent, const ClassCaptureAnchor& anchor,
    analysis::SemanticBudget budget) {
  try {
    auto work = budget.work;
    const auto step = [&](std::uint64_t count = 1) { Require(count <= work, Error::kWorkLimit); work -= count; };
    step();
    const auto& owner = anchor.owner;
    Require(owner.ClassContext() && owner.Identity() == cells.identity && owner.Target() == cells.child &&
        parent.tree.identity == cells.identity && parent.tree.method == cells.parent, Error::kOwner);
    Revision(parent, cells.parent_revision, cells.parent_cfg);
    Require(anchor.creation.value < parent.flow.expressions.size() && parent.flow.expressions[anchor.creation.value], Error::kCreation);
    const auto expression = *parent.flow.expressions[anchor.creation.value];
    Require(expression.value < parent.tree.expressions.size(), Error::kCreation);
    const auto* syntax = std::get_if<ClassExpr>(&parent.tree.expressions[expression.value].expression);
    Require(syntax && syntax->definition, Error::kCreation);
    const auto& layout = *syntax->definition;
    Require(layout.identity == cells.identity && layout.creation.creator == cells.parent, Error::kOwner);
    if (const auto* constructor = owner.ConstructorSite()) {
      Require(!owner.ClassContext()->Occurrence() && cells.creation == anchor.creation &&
          cells.input.pc == constructor->pc && cells.input.target == owner.Position() &&
          layout.creation.target == constructor->target && layout.creation.creator == constructor->creator &&
          layout.creation.pc == constructor->pc && layout.creation.creation == constructor->creation, Error::kCreation);
    } else if (const auto* member = owner.ClassSite()) {
      const auto occurrence = owner.ClassContext()->Occurrence();
      Require(occurrence && member->property < layout.member_sites.size() && layout.member_sites[member->property] == *occurrence &&
          cells.creation == anchor.creation && cells.input.pc == member->pc && cells.input.target == owner.Position() &&
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
          found->site.creation == anchor.creation && found->site.Method(defined->kind) == cells.creation &&
          defined->placement == (found->site.prototype ? abc::ClassPlacement::kPrototype : abc::ClassPlacement::kStatic), Error::kCreation);
    }
    return budget.work - work;
  } catch (Error error) { return error; }
}
}  // namespace abcd::source
