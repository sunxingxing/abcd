#include "../../../src/analysis/value_phi.h"
#include "../../../src/analysis/value_edits.h"
#include "../../../src/analysis/value_flow.h"

#include <array>
#include <cstdlib>

namespace {
using namespace abcd::analysis;
void Check(bool condition) {
  if (!condition) std::abort();
}
void Append(ValueProgram& graph, OperandUse use) {
  const UseId id{static_cast<std::uint32_t>(graph.uses.size())};
  auto& value = graph.values[std::get<ValueId>(use.operand).value];
  use.next = value.first_use;
  if (use.next) graph.uses[use.next->value].previous = id;
  value.first_use = id;
  graph.uses.push_back(use);
}
enum class PhiShape { kChain, kRing };
constexpr std::uint32_t kFirstPhi = 2;
ValueProgram LoopPhis(std::uint32_t count, PhiShape shape, bool mixed = false) {
  ValueProgram graph;
  graph.blocks = {{{0}, 1, {0}, 2}, {{1}, 1, {2}, 0}};
  graph.edges = {{{0}, {0}, FlowEdgeKind::kTaken, 0},
                 {{0}, {1}, FlowEdgeKind::kFallthrough, 1}};
  graph.origins = {{{0}, {0}, {}}, {{1}, {1}, {}}};
  graph.values.push_back({EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}});
  graph.values.push_back({EntryDefinition{ParameterEntry{1}}, ValueKinds::Language(), {}});
  for (std::uint32_t i = 0; i < count; ++i)
    graph.values.push_back({PhiDefinition{{0}}, ValueKinds::Language(), {}});
  graph.ops.push_back({ValueBranch{UnaryOperator::kToBoolean}, {0}, 0, {0}, 1, {},
      LanguageEffect::kNone, LanguageEffect::kNone,
      ExceptionalCompletion::kImpossible, {0}});
  graph.ops.push_back({ValueReturn{}, {1}, 0, {1}, 1, {},
      LanguageEffect::kNone, LanguageEffect::kNone,
      ExceptionalCompletion::kImpossible, {1}});
  Append(graph, {OpId{0}, OperandRole::kCondition, ReadPurpose::kOperand,
      ValueId{0}, {}, {}, {}, {}});
  Append(graph, {OpId{1}, OperandRole::kReturnValue, ReadPurpose::kOperand,
      ValueId{kFirstPhi}, {}, {}, {}, {}});
  for (std::uint32_t i = 0; i < count; ++i) {
    const ValueId phi{kFirstPhi + i};
    const auto next = i + 1 == count ? (shape == PhiShape::kRing ? 0 : i) : i + 1;
    const ValueId entry{mixed && i + 1 == count ? 1U : 0U};
    Append(graph, {phi, OperandRole::kPhiInput, ReadPurpose::kOperand,
        entry, MethodEntryKey{}, {}, {}, {}});
    Append(graph, {phi, OperandRole::kPhiInput, ReadPurpose::kOperand,
        ValueId{kFirstPhi + next}, EdgeId{0}, {}, {}, {}});
  }
  return graph;
}
}  // namespace

int main() {
  constexpr std::array<std::uint32_t, 4> kSizes{1, 200, 201, 4096};
  for (const auto count : kSizes) {
    for (const auto shape : {PhiShape::kChain, PhiShape::kRing}) {
      auto graph = LoopPhis(count, shape);
      Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(graph)));
      auto probe = graph;
      const auto collapsed = SimplifyPhis(probe, {});
      const auto* stats = std::get_if<PhiSimplifyStats>(&collapsed);
      Check(stats && stats->collapsed_values == count);
      constexpr std::uint64_t kWorkPerOccurrenceBound = 128;
      Check(stats->work <= kWorkPerOccurrenceBound * (graph.values.size() + graph.uses.size()));
      Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(probe)));
      const auto result = SimplifyValues(graph, {});
      const auto* batch = std::get_if<ValueSimplifyStats>(&result);
      Check(batch && batch->removed_phis == count && batch->removed_ops == 0);
      Check(std::get<ValueId>(graph.uses[1].operand) == ValueId{0});
      Check(graph.revision == IrRevision{1});
      Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(graph)));
    }
  }
  auto graph = LoopPhis(3, PhiShape::kRing, true);
  const auto retained = SimplifyValues(graph, {});
  const auto* stats = std::get_if<ValueSimplifyStats>(&retained);
  Check(stats && stats->replaced_uses == 0 && stats->removed_phis == 0);
  Check(graph.revision == IrRevision{0});
  graph = LoopPhis(1, PhiShape::kRing);
  graph.values[0].domain = ValueKinds::Defined();
  const auto refined = SimplifyValues(graph, {});
  Check(std::get<ValueSimplifyStats>(refined).removed_phis == 1);
  graph = LoopPhis(1, PhiShape::kRing);
  graph.values[kFirstPhi].domain = ValueKinds::Defined();
  const auto weaker = SimplifyPhis(graph, {});
  Check(std::get<PhiSimplifyStats>(weaker).collapsed_values == 0);
  graph = LoopPhis(1, PhiShape::kRing);
  // Deliberately malformed entry tests the SCC algorithm's no-anchor rule;
  // the full batch must reject this graph at its earlier flow boundary.
  Check(!ReplaceUse(graph, UseId{2}, ValueId{kFirstPhi}));
  const auto self = SimplifyPhis(graph, {});
  Check(std::get<PhiSimplifyStats>(self).collapsed_values == 0);
  Check(std::get<ValueSimplifyError>(SimplifyValues(graph, {})) ==
        ValueSimplifyError::kInvalidGraph);
}
