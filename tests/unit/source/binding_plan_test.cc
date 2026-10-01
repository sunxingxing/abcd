#include "../../../src/source/binding_plan.h"

#include <cstdlib>

namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool condition) { if (!condition) std::abort(); }
analysis::ValueProgram Returning(analysis::EntryDefinition entry) {
  using namespace analysis;
  ValueProgram graph;
  graph.values.push_back({entry, ValueKinds::Language(), UseId{0}});
  graph.uses.push_back({OpId{0}, OperandRole::kReturnValue,
                       ReadPurpose::kOperand, ValueId{0}, {}, {}, {}, {}});
  graph.ops.push_back({ValueReturn{}, {0}, 0, {0}, 1, {},
      LanguageEffect::kNone, LanguageEffect::kNone,
      ExceptionalCompletion::kImpossible, {0}});
  graph.blocks.push_back({{0}, 1, {0}, 0});
  return graph;
}
void ParameterAndUnusedFormals() {
  auto graph = Returning(analysis::ParameterEntry{0});
  auto evaluation = BuildEvaluationPlan(graph);
  Check(std::holds_alternative<EvaluationPlan>(evaluation));
  for (const std::uint32_t count : {1U, 200U, 201U, 4096U}) {
    auto built = BuildBindingPlan(graph, std::get<EvaluationPlan>(evaluation), count);
    Check(std::holds_alternative<BindingPlan>(built));
    const auto& plan = std::get<BindingPlan>(built);
    Check(plan.symbols.size() == count);
    Check(std::get<SymbolId>(plan.values[0]).value == 0);
    Check(std::get<ParameterBinding>(plan.symbols.back()).index == count - 1);
    Check(plan.parameters.formals.size() == count);
    Check(plan.parameters.formals[0].entry == analysis::ValueId{0});
    if (count > 1) Check(!plan.parameters.formals.back().entry);
  }
  auto missing = BuildBindingPlan(graph, std::get<EvaluationPlan>(evaluation), 0);
  Check(std::get<BindingError>(missing) == BindingError::kParameter);
  auto limited = BuildBindingPlan(graph, std::get<EvaluationPlan>(evaluation), 1,
                                  {0, 0});
  Check(std::holds_alternative<BindingError>(limited));
}
void ReceiverAndUnsupportedAbi() {
  for (analysis::EntryDefinition entry : {
           analysis::EntryDefinition{analysis::ReceiverEntry{}},
           analysis::EntryDefinition{analysis::CallableEntry{}},
           analysis::EntryDefinition{analysis::NewTargetEntry{}}}) {
    auto graph = Returning(entry);
    auto evaluation = BuildEvaluationPlan(graph);
    Check(std::holds_alternative<EvaluationPlan>(evaluation));
    auto built = BuildBindingPlan(graph, std::get<EvaluationPlan>(evaluation), 0);
    if (std::holds_alternative<analysis::ReceiverEntry>(entry)) {
      Check(std::holds_alternative<BindingPlan>(built));
      const auto& plan = std::get<BindingPlan>(built);
      Check(plan.symbols.empty());
      Check(std::holds_alternative<ReceiverEntryBinding>(plan.values[0]));
    } else {
      Check(std::get<BindingError>(built) == BindingError::kUnsupportedEntry);
    }
  }
}
void SpreadProtocolEntry() {
  using namespace analysis;
  for (EntryDefinition entry : {EntryDefinition{CallableEntry{}},
                               EntryDefinition{NewTargetEntry{}},
                               EntryDefinition{UnknownEntry{abc::RegisterId{0}}}}) {
    ValueProgram graph;
    graph.values.push_back({entry, ValueKinds::Language(), UseId{0}});
    graph.values.push_back({EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), UseId{1}});
    graph.values.push_back({OpResult{OpId{0}, {}}, ValueKinds::Language(), UseId{2}});
    graph.uses.push_back({OpId{0}, OperandRole::kConstructor, ReadPurpose::kOperand, ValueId{0}, {}, {}, {}, {}});
    graph.uses.push_back({OpId{0}, OperandRole::kInput, ReadPurpose::kOperand, ValueId{1}, {}, {}, {}, {}});
    graph.uses.push_back({OpId{1}, OperandRole::kReturnValue, ReadPurpose::kOperand, ValueId{2}, {}, {}, {}, {}});
    graph.ops.push_back({ValueSuperSpread{0}, {0}, 0, {0}, 2, ValueId{2},
        LanguageEffect::kUserCode, LanguageEffect::kUserCode, ExceptionalCompletion::kMayThrow, {0}});
    graph.ops.push_back({ValueReturn{}, {0}, 1, {2}, 1, {},
        LanguageEffect::kNone, LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {1}});
    graph.blocks.push_back({{0}, 2, {0}, 0});
    const auto scheduled = BuildEvaluationPlan(graph);
    Check(std::holds_alternative<EvaluationPlan>(scheduled));
    const auto built = BuildBindingPlan(graph, std::get<EvaluationPlan>(scheduled), 1);
    if (std::holds_alternative<CallableEntry>(entry)) {
      Check(std::holds_alternative<BindingPlan>(built));
      Check(std::holds_alternative<UnboundValue>(std::get<BindingPlan>(built).values[0]));
      // It remains a protocol operand, never a fabricated source local.
      Check(!std::get<BindingPlan>(built).self);
    } else Check(std::get<BindingError>(built) == BindingError::kUnsupportedEntry);
    graph.uses[1].role = OperandRole::kArgument;
    Check(std::holds_alternative<EvaluationError>(BuildEvaluationPlan(graph)));
  }
}
}  // namespace
int main() {
  ParameterAndUnusedFormals();
  ReceiverAndUnsupportedAbi();
  SpreadProtocolEntry();
}
