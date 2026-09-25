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
      Check(std::holds_alternative<ReceiverBinding>(plan.values[0]));
    } else {
      Check(std::get<BindingError>(built) == BindingError::kUnsupportedEntry);
    }
  }
}
}  // namespace
int main() {
  ParameterAndUnusedFormals();
  ReceiverAndUnsupportedAbi();
}
