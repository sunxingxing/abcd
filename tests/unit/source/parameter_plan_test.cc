#include "../../../src/source/parameter_plan.h"

#include <cstdlib>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
void Check(bool condition) { if (!condition) std::abort(); }
ValueProgram Entries(std::uint32_t count) {
  ValueProgram graph;
  for (std::uint32_t i = 0; i < count; ++i)
    graph.values.push_back({EntryDefinition{ParameterEntry{i}}, ValueKinds::Language()});
  return graph;
}
void IdentitiesAndBounds() {
  for (const std::uint32_t count : {0U, 1U, 200U, 201U, 4096U}) {
    auto graph = Entries(count);
    auto built = BuildParameterPlan(graph, count + 1);
    Check(std::holds_alternative<ParameterPlan>(built));
    const auto& plan = std::get<ParameterPlan>(built);
    Check(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision);
    Check(plan.formals.size() == count + 1 && !plan.formals.back().entry);
    for (std::uint32_t i = 0; i < count; ++i) Check(plan.formals[i].entry == ValueId{i});
    Check(plan.work == std::uint64_t(count) * 2 + 1);
    Check(std::get<ParameterPlanError>(BuildParameterPlan(graph, count + 1, {0, 0})) ==
          ParameterPlanError::kResourceLimit);
    Check(std::get<ParameterPlanError>(BuildParameterPlan(graph, count + 1,
          {1024 * 1024, plan.work - 1})) == ParameterPlanError::kWorkLimit);
  }
}
void DistinctVersions() {
  auto graph = Entries(1);
  // A later operation result is a new value, never the formal's entry identity.
  graph.values.push_back({OpResult{{0}}, ValueKinds::Language()});
  graph.values.push_back({EntryDefinition{ReceiverEntry{}}, ValueKinds::Language()});
  const auto plan = std::get<ParameterPlan>(BuildParameterPlan(graph, 1));
  Check(plan.formals[0].entry == ValueId{0});
  graph.values.push_back({EntryDefinition{ParameterEntry{0}}, ValueKinds::Language()});
  Check(std::get<ParameterPlanError>(BuildParameterPlan(graph, 1)) == ParameterPlanError::kDuplicateEntry);
  graph.values.back().origin = EntryDefinition{ParameterEntry{1}};
  Check(std::get<ParameterPlanError>(BuildParameterPlan(graph, 1)) == ParameterPlanError::kOrdinal);
  graph.values.back().life = ValueLife::kRemoved;
  Check(std::holds_alternative<ParameterPlan>(BuildParameterPlan(graph, 1)));
}
}  // namespace
int main() { IdentitiesAndBounds(); DistinctVersions(); }
