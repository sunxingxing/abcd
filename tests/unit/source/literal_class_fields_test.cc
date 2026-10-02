#include "../../../src/source/literal_class_fields.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
ValueProgram Graph(std::uint32_t count) {
  ValueProgram graph;
  graph.values.push_back({EntryDefinition{ReceiverEntry{}},ValueKinds::Language(),UseId{0}});
  constexpr auto kEffects = LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto first = static_cast<std::uint32_t>(graph.uses.size());
    graph.uses.push_back({OpId{i},OperandRole::kBase,ReadPurpose::kOperand,ValueId{0},{},{},
        i ? std::optional{UseId{first-2}} : std::nullopt,
        i+1 < count ? std::optional{UseId{first+2}} : std::nullopt});
    graph.uses.push_back({OpId{i},OperandRole::kStoredValue,ReadPurpose::kOperand,IntegerLiteral{42}});
    graph.ops.push_back({ValueOwnWrite{OwnWriteKind::kDefineField,{i},0},{0},i,{first},2,{},kEffects,kEffects,ExceptionalCompletion::kMayThrow,{i}});
    graph.origins.push_back({{i},{i},{}});
    graph.exceptional_exits.push_back({OpId{i},{i},kEffects});
  }
  const auto first = static_cast<std::uint32_t>(graph.uses.size());
  graph.uses.push_back({OpId{count},OperandRole::kReturnValue,ReadPurpose::kOperand,SpecialValue::kUndefined});
  graph.ops.push_back({ValueReturn{},{0},count,{first},1,{},LanguageEffect::kNone,LanguageEffect::kNone,ExceptionalCompletion::kImpossible,{count}});
  graph.origins.push_back({{count},{count},{}});
  graph.blocks.push_back({{0},count+1,{0},0}); return graph;
}
}
int main() {
  std::uint64_t previous = 0;
  for (const auto count : {1U,2U,200U,201U,402U}) {
    const auto graph = Graph(count);
    const auto built = LiteralClassFields::Build(graph);
    test::Check(std::holds_alternative<LiteralClassFields>(built));
    const auto& proof = std::get<LiteralClassFields>(built);
    test::Check(proof.Fields().size() == count);
    if (count == 402) test::Check(proof.Work() < 3*previous);
    previous = proof.Work();
    SemanticBudget budget; budget.work = proof.Work();
    test::Check(std::holds_alternative<LiteralClassFields>(LiteralClassFields::Build(graph,budget)));
    --budget.work;
    test::Check(std::get<LiteralClassFieldError>(LiteralClassFields::Build(graph,budget)) == LiteralClassFieldError::kWorkLimit);
  }
  auto graph = Graph(1);
  graph.values[0].origin = EntryDefinition{ParameterEntry{0}};
  test::Check(std::get<LiteralClassFieldError>(LiteralClassFields::Build(graph)) == LiteralClassFieldError::kUnsupported);
  graph = Graph(1); graph.uses[2].operand = IntegerLiteral{1};
  test::Check(std::get<LiteralClassFieldError>(LiteralClassFields::Build(graph)) == LiteralClassFieldError::kUnsupported);
  graph = Graph(1); std::get<ValueOwnWrite>(graph.ops[0].operation).kind = OwnWriteKind::kStore;
  test::Check(std::get<LiteralClassFieldError>(LiteralClassFields::Build(graph)) == LiteralClassFieldError::kUnsupported);
  graph = Graph(1); graph.ops[0].operation = ValueNamedWrite{{0}};
  test::Check(std::get<LiteralClassFieldError>(LiteralClassFields::Build(graph)) == LiteralClassFieldError::kUnsupported);
  graph = Graph(1); graph.ops[0].normal = LanguageEffect::kNone;
  test::Check(std::holds_alternative<LiteralClassFieldError>(LiteralClassFields::Build(graph)));
  graph = Graph(1); graph.uses[1].operand = Float64Bits{0xffff000000000005ULL};
  test::Check(std::get<LiteralClassFieldError>(LiteralClassFields::Build(graph)) == LiteralClassFieldError::kUnsupported);
  test::Check(std::get<LiteralClassFieldError>(LiteralClassFields::Build(Graph(1),{0,1000})) == LiteralClassFieldError::kResourceLimit);
}
