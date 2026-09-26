#include "../../../src/source/phi_copies.h"

#include <bit>
#include <cstdlib>

namespace {
using namespace abcd::analysis;
using namespace abcd::source;
void Check(bool condition) { if (!condition) std::abort(); }
void Append(ValueProgram& graph, OperandUse use) {
  const UseId id{static_cast<std::uint32_t>(graph.uses.size())};
  auto& definition = graph.values[std::get<ValueId>(use.operand).value];
  use.next = definition.first_use;
  if (use.next) graph.uses[use.next->value].previous = id;
  definition.first_use = id;
  graph.uses.push_back(use);
}
ValueProgram Graph(std::uint32_t count, bool loop) {
  ValueProgram graph;
  graph.blocks = {{{0}, 1, {0}, 2}, {{1}, 1, {2}, 0}};
  graph.edges = {{{0}, {loop ? 0U : 1U}, FlowEdgeKind::kTaken, 0},
                 {{0}, {1}, FlowEdgeKind::kFallthrough, 1}};
  graph.values.push_back({EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}});
  for (std::uint32_t i = 0; i < count; ++i)
    graph.values.push_back({PhiDefinition{{loop ? 0U : 1U}}, ValueKinds::Language(), {}});
  graph.ops.push_back({ValueBranch{UnaryOperator::kToBoolean}, {0}, 0, {0}, 1, {},
      LanguageEffect::kNone, LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {0}});
  graph.ops.push_back({ValueReturn{}, {1}, 0, {1}, 1, {},
      LanguageEffect::kNone, LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {1}});
  Append(graph, {OpId{0}, OperandRole::kCondition, ReadPurpose::kOperand, ValueId{0}, {}, {}, {}, {}});
  Append(graph, {OpId{1}, OperandRole::kReturnValue, ReadPurpose::kOperand, ValueId{1}, {}, {}, {}, {}});
  for (std::uint32_t i = 1; i <= count; ++i) {
    const IncomingKey first = loop ? IncomingKey{MethodEntryKey{}} : IncomingKey{EdgeId{0}};
    const IncomingKey second = EdgeId{loop ? 0U : 1U};
    Append(graph, {ValueId{i}, OperandRole::kPhiInput, ReadPurpose::kOperand, ValueId{0}, first, {}, {}, {}});
    Append(graph, {ValueId{i}, OperandRole::kPhiInput, ReadPurpose::kOperand,
        ValueId{loop ? i % count + 1 : 0}, second, {}, {}, {}});
  }
  return graph;
}
PhiCopies Plan(const ValueProgram& graph) {
  auto built = BuildPhiCopies(graph);
  Check(std::holds_alternative<PhiCopies>(built));
  auto plan = std::get<PhiCopies>(std::move(built));
  Check(std::holds_alternative<std::uint64_t>(VerifyPhiCopies(graph, plan)));
  return plan;
}
void Mutations() {
  auto graph = Graph(2, false);
  const auto plan = Plan(graph);
  Check(plan.bundles.size() == 2 && plan.copies.size() == 4);
  Check(std::get<EdgeId>(plan.bundles[0].incoming) == EdgeId{0});
  Check(std::get<EdgeId>(plan.bundles[1].incoming) == EdgeId{1});
  auto changed = plan;
  ++changed.revision.value;
  Check(std::get<PhiCopyError>(VerifyPhiCopies(graph, changed)) == PhiCopyError::kRevision);
  changed = plan; ++changed.cfg_revision.value;
  Check(std::get<PhiCopyError>(VerifyPhiCopies(graph, changed)) == PhiCopyError::kRevision);
  changed = plan; changed.inputs[1] = changed.inputs[0];
  Check(std::get<PhiCopyError>(VerifyPhiCopies(graph, changed)) == PhiCopyError::kInput);
  changed = plan; changed.copies[0].source = ValueId{1};
  Check(std::get<PhiCopyError>(VerifyPhiCopies(graph, changed)) == PhiCopyError::kInput);
  changed = plan; changed.bundles[0].incoming = MethodEntryKey{};
  Check(std::get<PhiCopyError>(VerifyPhiCopies(graph, changed)) == PhiCopyError::kInput);
  changed = plan; changed.bundles.pop_back();
  Check(std::get<PhiCopyError>(VerifyPhiCopies(graph, changed)) == PhiCopyError::kBundle);
  changed = plan; changed.bundles[0].schedule.steps.clear();
  Check(std::get<PhiCopyError>(VerifyPhiCopies(graph, changed)) == PhiCopyError::kSchedule);
  graph.values[1].domain = ValueKinds::Hole();
  Check(std::get<PhiCopyError>(BuildPhiCopies(graph)) == PhiCopyError::kDomain);
  graph = Graph(2, false); graph.uses[3].incoming = EdgeId{0};
  Check(std::get<PhiCopyError>(BuildPhiCopies(graph)) == PhiCopyError::kFlow);
  graph = Graph(2, false);
  SemanticBudget budget;
  budget.work = plan.work;
  Check(std::holds_alternative<PhiCopies>(BuildPhiCopies(graph, budget)));
  --budget.work;
  Check(std::get<PhiCopyError>(BuildPhiCopies(graph, budget)) == PhiCopyError::kWorkLimit);
  budget = {}; budget.bytes = 0;
  Check(std::get<PhiCopyError>(BuildPhiCopies(graph, budget)) == PhiCopyError::kResourceLimit);
}
void EntryAndScaling() {
  for (const std::uint32_t count : {1U, 2U, 200U, 201U, 4096U}) {
    for (const bool loop : {false, true}) {
      const auto graph = Graph(count, loop);
      const auto plan = Plan(graph);
      Check(plan.bundles.size() == 2);
      Check(plan.bundles[0].count == count && plan.bundles[1].count == count);
      Check(std::holds_alternative<MethodEntryKey>(plan.bundles[0].incoming) == loop);
      Check(plan.bundles[0].schedule.temporaries == 0);
      Check(plan.bundles[1].schedule.temporaries == (loop && count > 1 ? 1U : 0U));
      constexpr std::uint64_t kWorkPerInputLogBound = 512;
      Check(plan.work <= kWorkPerInputLogBound * graph.uses.size() * std::bit_width(graph.uses.size()));
    }
  }
}
}  // namespace
int main() { Mutations(); EntryAndScaling(); }
