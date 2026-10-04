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
ValueProgram CopyRing(std::uint32_t count, bool mixed) {
  ValueProgram graph;
  graph.blocks = {{{0},count+1,{0},2},{{count+1},1,{2},0}};
  graph.edges = {{{0},{0},FlowEdgeKind::kTaken,0},{{0},{1},FlowEdgeKind::kFallthrough,1}};
  graph.values = {{EntryDefinition{ParameterEntry{0}},ValueKinds::Language(),{}},
                  {EntryDefinition{ParameterEntry{1}},ValueKinds::Language(),{}}};
  for(std::uint32_t i=0;i<count;++i) graph.values.push_back({PhiDefinition{{0}},ValueKinds::Language(),{}});
  for(std::uint32_t i=0;i<count;++i) graph.values.push_back({OpResult{OpId{i},{}},ValueKinds::Language(),{}});
  for(std::uint32_t i=0;i<count;++i) {
    graph.ops.push_back({ValueCopy{},{0},i,{i},1,ValueId{2+count+i},LanguageEffect::kNone,LanguageEffect::kNone,ExceptionalCompletion::kImpossible,{i}});
    Append(graph,{OpId{i},OperandRole::kInput,ReadPurpose::kOperand,ValueId{2+(i+1)%count},{},{},{},{}});
  }
  graph.ops.push_back({ValueBranch{UnaryOperator::kToBoolean},{0},count,{count},1,{},LanguageEffect::kNone,LanguageEffect::kNone,ExceptionalCompletion::kImpossible,{count}});
  graph.ops.push_back({ValueReturn{},{1},0,{count+1},1,{},LanguageEffect::kNone,LanguageEffect::kNone,ExceptionalCompletion::kImpossible,{count+1}});
  Append(graph,{OpId{count},OperandRole::kCondition,ReadPurpose::kOperand,ValueId{0},{},{},{},{}});
  Append(graph,{OpId{count+1},OperandRole::kReturnValue,ReadPurpose::kOperand,ValueId{2},{},{},{},{}});
  for(std::uint32_t i=0;i<count;++i) {
    Append(graph,{ValueId{2+i},OperandRole::kPhiInput,ReadPurpose::kOperand,ValueId{mixed && i+1==count ? 1U : 0U},MethodEntryKey{},{},{},{}});
    Append(graph,{ValueId{2+i},OperandRole::kPhiInput,ReadPurpose::kOperand,ValueId{2+count+i},EdgeId{0},{},{},{}});
  }
  return graph;
}
void ReadOnlyIdentities() {
  std::uint64_t previous_work=0;
  for(const std::uint32_t count : {1U,200U,201U,402U}) {
    for(const bool mixed : {false,true}) {
      const auto graph=CopyRing(count,mixed);
      Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(graph)));
      const auto indexed=AnalyzeValueIdentities(graph);
      Check(std::holds_alternative<ValueIdentities>(indexed));
      const auto& identities=std::get<ValueIdentities>(indexed);
      for(std::uint32_t i=2;i<graph.values.size();++i)
        Check(identities.canonical[i] == ValueId{mixed ? (count == 1 ? 1U : i) : 0U});
      // A read-only projection leaves every original occurrence and equation.
      Check(std::get<ValueId>(graph.uses[count+1].operand) == ValueId{2});
      Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(graph)));
      if(!mixed) {
        if(previous_work && count>200) Check(identities.work <= previous_work*3);
        previous_work=identities.work;
      }
    }
  }
}
}  // namespace

int main() {
  ReadOnlyIdentities();
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
