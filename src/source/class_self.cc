#include "class_self.h"

#include <algorithm>
#include <new>
#include <stdexcept>

#include "effect_order.h"

namespace abcd::source {
std::variant<ClassSelfStores, ClassSelfError> FindClassSelfStores(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& cells, std::span<const ClassInitializerCall> requested, analysis::SemanticBudget budget) {
  using namespace analysis;
  using Error = ClassSelfError;
  const auto initial = budget;
  try {
    const auto require = [](bool condition, Error error) { if (!condition) throw error; };
    const auto step = [&](std::uint64_t count = 1) {
      require(count <= budget.work,Error::kWorkLimit); budget.work -= count;
    };
    const auto admit = [&](std::size_t count, std::size_t width) {
      require(count <= budget.bytes/width,Error::kResourceLimit); budget.bytes -= count*width;
    };
    const auto array = [&]<class T>(std::vector<T>& values, std::size_t count) {
      step(count); admit(count,sizeof(T)); values.resize(count);
      admit(values.capacity()-count,sizeof(T));
    };
    const auto flow = BuildEffectOrder(graph,budget);
    if (const auto* error = std::get_if<ValueAvailabilityError>(&flow)) {
      if (*error == ValueAvailabilityError::kResourceLimit) return Error::kResourceLimit;
      if (*error == ValueAvailabilityError::kWorkLimit) return Error::kWorkLimit;
      return Error::kFlow;
    }
    const auto& order = std::get<EffectOrder>(flow);
    admit(order.allocated_bytes,1); step(order.work);
    const auto verified = VerifyCellPlan(graph,environments,cells,budget);
    if (const auto* error = std::get_if<CellError>(&verified)) {
      if (*error == CellError::kResourceLimit) return Error::kResourceLimit;
      if (*error == CellError::kWorkLimit) return Error::kWorkLimit;
      return Error::kCells;
    }
    step(std::get<std::uint64_t>(verified));
    auto found = FindClassInitializerCalls(graph,budget);
    if (const auto* error = std::get_if<ClassInitializerError>(&found)) {
      if (*error == ClassInitializerError::kResourceLimit) return Error::kResourceLimit;
      if (*error == ClassInitializerError::kWorkLimit) return Error::kWorkLimit;
      return Error::kInput;
    }
    const auto& calls = std::get<ClassInitializerCalls>(found);
    admit(calls.allocated_bytes,1); step(calls.work);
    std::vector<std::optional<OpId>> initializers;
    array(initializers,graph.ops.size());
    for (const auto& call : requested) {
      step();
      const auto known = std::lower_bound(calls.calls.begin(),calls.calls.end(),call.method,
          [&](const auto& candidate, auto id) { step(); return candidate.method < id; });
      require(known != calls.calls.end() && *known == call && call.creation.value < initializers.size() &&
          !initializers[call.creation.value],Error::kInput);
      initializers[call.creation.value] = call.call;
    }
    ClassSelfStores result; admit(1,sizeof(result));
    admit(cells.cells.size(),sizeof(ClassSelfStore)); result.stores.reserve(cells.cells.size());
    admit(result.stores.capacity()-cells.cells.size(),sizeof(ClassSelfStore));
    // CellPlan already groups accesses. Count writes once per group; no
    // per-class whole-graph walk and no guessing from a debug scope literal.
    for (std::size_t i = 0; i < cells.cells.size(); ++i) {
      step(); const auto& cell = cells.cells[i];
      const auto* local = std::get_if<AllocatedCell>(&cell.storage);
      if (!local) continue;
      std::optional<OpId> store;
      bool multiple = false;
      for (std::uint32_t j = 0; j < cell.count; ++j) {
        step(); const auto& access = cells.accesses[cell.first+j];
        if (access.kind != CellAccessKind::kStore) continue;
        if (store) multiple = true;
        else store = access.operation;
      }
      if (!store || multiple) continue;
      const auto& write = graph.ops[store->value];
      if (write.use_count != 1) continue;
      const auto* value = std::get_if<ValueId>(&graph.uses[write.first_use.value].operand);
      const auto* source = value ? std::get_if<OpResult>(&graph.values[value->value].origin) : nullptr;
      if (!source || !initializers[source->op.value]) continue;
      const auto& creation = graph.ops[source->op.value];
      const auto call = *initializers[source->op.value];
      const auto& invoke = graph.ops[call.value];
      if (!std::holds_alternative<ValueClassCreation>(creation.operation) ||
          !order.Before(graph,local->allocation,creation.block,creation.order) ||
          !order.Before(graph,source->op,write.block,write.order) ||
          !order.Before(graph,*store,invoke.block,invoke.order)) continue;
      // Only the fresh base-class creation may intervene before this first
      // store. No call/closure/other effect can expose the new environment.
      // Shared past identity and future must-reach discharge the private
      // allocation-to-store interval. Equal counts or block layout cannot prove
      // no observer, bypass, divergence or an earlier allocation activation.
      if (creation.use_count != 1 || order.before[source->op.value] != EffectPredecessor{local->allocation} ||
          order.after[local->allocation.value] != source->op ||
          order.before[store->value] != EffectPredecessor{source->op} ||
          order.after[source->op.value] != *store) continue;
      const auto* heritage = std::get_if<ValueId>(&graph.uses[creation.first_use.value].operand);
      if (!heritage || graph.values[heritage->value].domain != ValueKinds::Hole()) continue;
      bool observed = false;
      for (std::uint32_t j = 0; j < cell.count; ++j) {
        step(); const auto& access = cells.accesses[cell.first+j];
        if (access.kind != CellAccessKind::kLoad) continue;
        const auto& load = graph.ops[access.operation.value];
        if (!order.Before(graph,call,load.block,load.order)) observed = true;
      }
      if (observed) continue;
      result.stores.push_back({source->op,*store,call,CellId{static_cast<std::uint32_t>(i)},local->allocation});
    }
    result.allocated_bytes = sizeof(result)+result.stores.capacity()*sizeof(ClassSelfStore);
    result.work = initial.work-budget.work;
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
