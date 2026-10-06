#include "parameter_cell_names.h"
#include "../analysis/value_phi.h"
#include <algorithm>
#include <bit>
#include <new>
#include <stdexcept>
namespace abcd::source {
namespace {
using Error = ParameterCellNameError;
enum class HintState { kAbsent, kUnique, kConflicting };
struct Hint { HintState state = HintState::kAbsent; std::optional<NameId> name; };
struct Proposal { std::size_t parameter; NameId name; bool existing; };
}
std::variant<ParameterCellNameCost, ParameterCellNameError> NameCellParameters(
    const analysis::ValueProgram& graph, const CellPlan& cells, FunctionSyntax& syntax,
    analysis::SemanticBudget budget) {
  try {
    const auto initial = budget;
    const auto require = [](bool value) { if (!value) throw Error::kInput; };
    const auto step = [&](std::uint64_t count = 1) {
      if (count > budget.work) throw Error::kWorkLimit; budget.work -= count;
    };
    const auto admit = [&](std::size_t count, std::size_t width) {
      if (count > budget.bytes / width) throw Error::kResourceLimit; budget.bytes -= count * width;
    };
    require(cells.revision == graph.revision && cells.cfg_revision == graph.cfg_revision &&
        cells.operations.size() == graph.ops.size());
    if (syntax.parameters.empty() || syntax.locals.empty()) return ParameterCellNameCost{};
    auto analyzed = analysis::AnalyzeValueIdentities(graph, budget);
    if (const auto* error = std::get_if<analysis::ValueSimplifyError>(&analyzed)) {
      if (*error == analysis::ValueSimplifyError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == analysis::ValueSimplifyError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kInput;
    }
    const auto& identities = std::get<analysis::ValueIdentities>(analyzed);
    admit(identities.allocated_bytes, 1); step(identities.work);
    admit(syntax.parameters.size(), sizeof(Hint));
    std::vector<Hint> hints(syntax.parameters.size());
    admit(hints.capacity() - hints.size(), sizeof(Hint));
    const auto text = [&](NameId id) -> const std::u16string& {
      require(id.value < syntax.names.size()); return syntax.names[id.value];
    };
    const auto equal = [&](NameId a, NameId b) {
      step(std::min(text(a).size(), text(b).size()) + 1); return text(a) == text(b);
    };
    for (const auto& cell : cells.cells) {
      step();
      const auto* local = std::get_if<AllocatedCell>(&cell.storage);
      if (!local) continue; // Incoming names belong to a different activation.
      const auto key = std::pair{local->allocation.value, local->slot.value};
      step(std::max<std::size_t>(1, std::bit_width(syntax.locals.size())));
      const auto named = std::lower_bound(syntax.locals.begin(), syntax.locals.end(), key,
          [](const LocalNameSyntax& hint, const auto& target) {
            return std::pair{hint.allocation.value, hint.slot.value} < target;
          });
      if (named == syntax.locals.end() ||
          std::pair{named->allocation.value, named->slot.value} != key) continue;
      require(cell.first <= cells.accesses.size() && cell.count <= cells.accesses.size() - cell.first);
      for (const auto& access : std::span(cells.accesses).subspan(cell.first, cell.count)) {
        step(); if (access.kind != CellAccessKind::kStore) continue;
        require(access.operation.value < graph.ops.size());
        const auto& op = graph.ops[access.operation.value];
        require(op.life == analysis::ValueLife::kLive &&
            std::holds_alternative<analysis::ValueLexicalStore>(op.operation) && op.use_count == 1 &&
            op.first_use.value < graph.uses.size());
        const auto& use = graph.uses[op.first_use.value];
        require(use.life == analysis::ValueLife::kLive && use.owner == decltype(use.owner){access.operation} &&
            use.role == analysis::OperandRole::kStoredValue);
        const auto* value = std::get_if<analysis::ValueId>(&use.operand);
        if (!value) continue;
        require(value->value < identities.canonical.size());
        const auto root = identities.canonical[value->value];
        require(root.value < graph.values.size());
        const auto* entry = std::get_if<analysis::EntryDefinition>(&graph.values[root.value].origin);
        const auto* parameter = entry ? std::get_if<analysis::ParameterEntry>(entry) : nullptr;
        if (!parameter) continue;
        require(parameter->index < hints.size());
        if (syntax.parameters[parameter->index].name) continue;
        auto& hint = hints[parameter->index];
        if (hint.state == HintState::kAbsent) hint = {HintState::kUnique, named->name};
        else if (hint.state == HintState::kUnique && !equal(*hint.name, named->name))
          hint.state = HintState::kConflicting;
      }
    }
    // Decide all spelling collisions together. Do not let parameter order or
    // store order choose which original value receives an ambiguous name.
    admit(syntax.parameters.size(), sizeof(Proposal));
    std::vector<Proposal> proposals; proposals.reserve(syntax.parameters.size());
    admit(proposals.capacity() - syntax.parameters.size(), sizeof(Proposal));
    for (std::size_t i = 0; i < hints.size(); ++i) {
      step();
      if (syntax.parameters[i].name) proposals.push_back({i, *syntax.parameters[i].name, true});
      else if (hints[i].state == HintState::kUnique) proposals.push_back({i, *hints[i].name, false});
    }
    std::sort(proposals.begin(), proposals.end(), [&](const auto& a, const auto& b) {
      step(std::min(text(a.name).size(), text(b.name).size()) + 1); return text(a.name) < text(b.name);
    });
    for (std::size_t first = 0; first < proposals.size();) {
      auto end = first + 1;
      while (end < proposals.size() && equal(proposals[first].name, proposals[end].name)) ++end;
      if (end != first + 1) for (auto i = first; i < end; ++i) {
        step(); if (!proposals[i].existing) hints[proposals[i].parameter].state = HintState::kConflicting;
      }
      first = end;
    }
    step(hints.size());
    for (std::size_t i = 0; i < hints.size(); ++i) {
      if (hints[i].state == HintState::kUnique)
        syntax.parameters[i] = {*hints[i].name, NameEvidence::kMetadata, syntax.parameters[i].rest};
    }
    return ParameterCellNameCost{initial.bytes - budget.bytes, initial.work - budget.work};
  } catch (Error error) { return error; }
    catch (const std::bad_alloc&) { return Error::kResourceLimit; }
    catch (const std::length_error&) { return Error::kResourceLimit; }
}
}
