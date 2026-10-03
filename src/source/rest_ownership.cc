#include "rest_ownership.h"
#include "abcd/analysis/argument_window.h"
#include "abcd/abc/register_access.h"
#include <new>
#include <stdexcept>
namespace abcd::source {
std::variant<RestOwnership, RestOwnershipError> ProveRestOwnership(const analysis::ValueProgram& graph,
    const abc::MethodBody& body, const abc::FrameLayout& frame, std::uint16_t first, analysis::SemanticBudget budget) {
  using namespace analysis; using Error = RestOwnershipError;
  try {
    const auto initial = budget;
    const auto step = [&](std::uint64_t count = 1) {if (count > budget.work) throw Error::kWorkLimit; budget.work -= count;};
    const auto admit = [&](std::size_t count, std::size_t width) {if (count > budget.bytes / width) throw Error::kResourceLimit; budget.bytes -= count * width;};
    const auto window_result = ArgumentWindow::Build(frame);
    const auto* window = std::get_if<ArgumentWindow>(&window_result);
    if (!window || window->Count() != std::uint32_t{first} + 1 || !graph.phases.empty()) return Error::kFrame;
    std::optional<OpId> allocation;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step(); const auto& op = graph.ops[i]; if (op.life != ValueLife::kLive) continue;
      if (std::holds_alternative<ValueArgumentSnapshot>(op.operation)) return Error::kArgumentUse;
      if (const auto* rest = std::get_if<ValueRestArguments>(&op.operation)) {
        if (allocation || rest->first_argument != first) return Error::kAllocation;
        allocation = OpId{static_cast<std::uint32_t>(i)};
      }
    }
    if (!allocation) return Error::kAllocation;
    const auto& rest = graph.ops[allocation->value];
    // Reverse reachability indexes all possible work before the allocation.
    // The same traversal exposes a cycle through its block; native rest cannot
    // replace distinct arrays allocated on repeated loop iterations.
    admit(graph.blocks.size(), sizeof(std::vector<WorkBlockId>));
    std::vector<std::vector<WorkBlockId>> predecessors(graph.blocks.size());
    std::vector<std::uint32_t> counts;
    admit(graph.blocks.size(), sizeof(std::uint32_t)); counts.resize(graph.blocks.size());
    for (const auto& edge : graph.edges) {step(); ++counts[edge.target.value];}
    for (std::size_t b = 0; b < counts.size(); ++b) {
      step(); admit(counts[b], sizeof(WorkBlockId)); predecessors[b].reserve(counts[b]);
      admit(predecessors[b].capacity() - counts[b], sizeof(WorkBlockId));
    }
    for (const auto& edge : graph.edges) {step(); predecessors[edge.target.value].push_back(edge.source);}
    admit(graph.blocks.size(), sizeof(std::uint8_t)); std::vector<std::uint8_t> before(graph.blocks.size());
    admit(graph.blocks.size(), sizeof(WorkBlockId)); std::vector<WorkBlockId> pending; pending.reserve(graph.blocks.size());
    admit(pending.capacity() - graph.blocks.size(), sizeof(WorkBlockId));
    before[rest.block.value] = 1; pending.push_back(rest.block);
    for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) for (const auto prior : predecessors[pending[cursor].value]) {
      step(); if (prior == rest.block) return Error::kAllocation;
      if (!before[prior.value]) {before[prior.value] = 1; pending.push_back(prior);}
    }
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step(); const auto& op = graph.ops[i];
      if (op.life != ValueLife::kLive || !before[op.block.value] ||
          (op.block == rest.block && op.order >= rest.order)) continue;
      if (op.normal != LanguageEffect::kNone || op.before_throw != LanguageEffect::kNone ||
          op.exceptional != ExceptionalCompletion::kImpossible) return Error::kOrder;
    }
    // Original physical slots are implicit COPYRESTARGS inputs. Check writes
    // against original instruction coordinates, rather than printed bindings.
    const auto physical = *window->At(first);
    if (graph.input.sites.size() != body.instructions.size()) return Error::kFrame;
    for (std::size_t i = 0; i < body.instructions.size(); ++i) {
      step(); const auto& instruction = body.instructions[i];
      const auto mapped = graph.input.sites[i];
      if (!mapped || mapped->value >= graph.ops.size()) continue;
      const auto& op = graph.ops[mapped->value];
      if (!before[op.block.value] || (op.block == rest.block && op.order >= rest.order)) continue;
      const auto accesses = abc::GetRegisterAccesses(instruction.decoded, frame.registers);
      const auto* known = std::get_if<abc::RegisterAccesses>(&accesses); if (!known) return Error::kFrame;
      for (std::size_t i = 0; i < known->count; ++i) {
        step(); const auto& range = known->windows[i];
        if (range.access != abc::OperandAccess::kRead && physical.value >= range.first.value &&
            physical.value - range.first.value < range.count) return Error::kArgumentUse;
      }
    }
    // Follow the last declared scalar's transparent def-use transports once.
    // It may be copied and overwritten, but cannot become an observable array
    // value merely because the source signature now binds ...rest.
    admit(graph.values.size(), sizeof(std::uint8_t)); std::vector<std::uint8_t> tainted(graph.values.size());
    admit(graph.values.size(), sizeof(ValueId)); std::vector<ValueId> values; values.reserve(graph.values.size());
    admit(values.capacity() - graph.values.size(), sizeof(ValueId));
    const auto mark = [&](ValueId id) {if (!tainted[id.value]) {tainted[id.value] = 1; values.push_back(id);}};
    for (std::size_t i = 0; i < graph.values.size(); ++i) {
      step(); const auto* entry = std::get_if<EntryDefinition>(&graph.values[i].origin);
      const auto* parameter = entry ? std::get_if<ParameterEntry>(entry) : nullptr;
      if (parameter && parameter->index == first) mark({static_cast<std::uint32_t>(i)});
    }
    for (std::size_t cursor = 0; cursor < values.size(); ++cursor) {
      for (auto use = graph.values[values[cursor].value].first_use; use; use = graph.uses[use->value].next) {
        step(); const auto& occurrence = graph.uses[use->value]; if (occurrence.life != ValueLife::kLive) continue;
        if (const auto* phi = std::get_if<ValueId>(&occurrence.owner)) {mark(*phi); continue;}
        const auto* owner = std::get_if<OpId>(&occurrence.owner); if (!owner) return Error::kArgumentUse;
        const auto& op = graph.ops[owner->value]; if (op.life != ValueLife::kLive) continue;
        if (!std::holds_alternative<ValueCopy>(op.operation) || !op.result) return Error::kArgumentUse;
        mark(*op.result);
      }
    }
    return RestOwnership{initial.bytes - budget.bytes, initial.work - budget.work};
  } catch (const RestOwnershipError error) {return error;}
    catch (const std::bad_alloc&) {return RestOwnershipError::kResourceLimit;}
    catch (const std::length_error&) {return RestOwnershipError::kResourceLimit;}
}
}
