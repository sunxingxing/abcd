#include "parameter_plan.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
std::variant<ParameterPlan, ParameterPlanError> BuildParameterPlan(
    const analysis::ValueProgram& graph, std::uint32_t parameter_count,
    analysis::SemanticBudget budget) {
  using Error = ParameterPlanError;
  constexpr auto kMaxValue = std::numeric_limits<std::uint32_t>::max();
  if (graph.values.size() > kMaxValue || budget.bytes < sizeof(ParameterPlan))
    return Error::kResourceLimit;
  auto bytes = budget.bytes - sizeof(ParameterPlan);
  if (parameter_count > bytes / sizeof(ParameterEntryValue)) return Error::kResourceLimit;
  if (parameter_count > budget.work || graph.values.size() > budget.work - parameter_count)
    return Error::kWorkLimit;
  try {
    ParameterPlan plan;
    plan.revision = graph.revision;
    plan.cfg_revision = graph.cfg_revision;
    plan.formals.resize(parameter_count);
    if (plan.formals.capacity() > bytes / sizeof(ParameterEntryValue))
      return Error::kResourceLimit;
    bytes -= plan.formals.capacity() * sizeof(ParameterEntryValue);
    for (std::size_t i = 0; i < graph.values.size(); ++i) {
      const auto& value = graph.values[i];
      if (value.life != analysis::ValueLife::kLive) continue;
      const auto* entry = std::get_if<analysis::EntryDefinition>(&value.origin);
      const auto* parameter = entry ? std::get_if<analysis::ParameterEntry>(entry) : nullptr;
      if (!parameter) continue;
      if (parameter->index >= parameter_count) return Error::kOrdinal;
      auto& formal = plan.formals[parameter->index];
      if (formal.entry) return Error::kDuplicateEntry;
      formal.entry = analysis::ValueId{static_cast<std::uint32_t>(i)};
    }
    plan.allocated_bytes = budget.bytes - bytes;
    plan.work = parameter_count + graph.values.size();
    return plan;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
