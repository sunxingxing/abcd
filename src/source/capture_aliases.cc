#include "capture_aliases.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using Error = CaptureTreeError;
void Require(bool condition, Error error) { if (!condition) throw error; }
}  // namespace
std::variant<CaptureAliases, CaptureTreeError> BindCaptureAliases(
    const CaptureCells& cells, CaptureTree parent, FunctionOwnerId child,
    NameId first_name, analysis::SemanticBudget budget) {
  try {
    auto bytes = budget.bytes;
    auto work = budget.work;
    const auto step = [&](std::uint64_t count = 1) {
      Require(count <= work, Error::kWorkLimit); work -= count;
    };
    const auto admit = [&](std::size_t count, std::size_t width) {
      Require(count <= bytes / width, Error::kResourceLimit); bytes -= count * width;
    };
    const auto reserve = [&]<class T>(std::vector<T>& values, std::size_t count) {
      admit(count, sizeof(T)); values.reserve(count); admit(values.capacity() - count, sizeof(T));
    };
    step();
    const auto& tree = parent.tree;
    const auto& flow = parent.flow;
    Require(tree.owner != child && tree.identity == cells.identity && tree.method == cells.parent, Error::kOwner);
    Require(tree.value_revision == cells.parent_revision && tree.cfg_revision == cells.parent_cfg &&
        flow.value_revision == cells.parent_revision && flow.cfg_revision == cells.parent_cfg, Error::kRevision);
    Require(cells.creation.value < flow.expressions.size() && flow.expressions[cells.creation.value], Error::kCreation);
    const auto expression = *flow.expressions[cells.creation.value];
    Require(expression.value < tree.expressions.size(), Error::kCreation);
    const auto* creation = std::get_if<FunctionExpr>(&tree.expressions[expression.value].expression);
    Require(creation && creation->child == child, Error::kCreation);
    const auto range = creation->captures;
    Require(range.first <= tree.closure_captures.size() && range.count <= tree.closure_captures.size() - range.first &&
        range.count == cells.targets.size(), Error::kCoverage);
    Require(cells.targets.size() <= std::numeric_limits<std::uint32_t>::max() - first_name.value, Error::kResourceLimit);
    CaptureAliases result;
    admit(1, sizeof(result));
    reserve(result.aliases, cells.targets.size()); reserve(result.names, cells.targets.size());
    for (std::size_t i = 0; i < cells.targets.size(); ++i) {
      step();
      const auto& binding = cells.targets[i];
      Require(i == 0 || cells.targets[i - 1].child < binding.child, Error::kCoverage);
      const auto* target = std::get_if<ParentCell>(&binding.target);
      Require(target != nullptr, Error::kOuterCapture);
      Require(target->cell.value < flow.cells.size() && flow.cells[target->cell.value], Error::kCell);
      const auto symbol = *flow.cells[target->cell.value];
      Require(symbol.value < tree.symbols.size() && tree.closure_captures[range.first + i] == symbol, Error::kTarget);
      const auto& source = tree.symbols[symbol.value];
      Require(source.kind == BindingKind::kLocal || source.kind == BindingKind::kCapture, Error::kCell);
      Require(source.name.value < tree.names.size(), Error::kName);
      const auto& name = tree.names[source.name.value];
      step(name.size()); admit(name.size(), sizeof(char16_t)); admit(1, sizeof(char16_t));
      auto& copied = result.names.emplace_back(name);
      admit(copied.capacity() - name.size(), sizeof(char16_t));
      result.aliases.push_back({binding.child, {tree.owner, symbol},
          {first_name.value + static_cast<std::uint32_t>(i)}, source.evidence});
    }
    result.allocated_bytes = budget.bytes - bytes;
    result.work = budget.work - work;
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
