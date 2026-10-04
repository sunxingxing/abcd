#include "../../../src/source/literal_class_fields.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
ValueProgram Graph(std::uint32_t count, bool frame = false) {
  ValueProgram graph;
  graph.values.push_back({EntryDefinition{ReceiverEntry{}},ValueKinds::Language(),UseId{0}});
  const std::uint32_t prefix = frame ? 1 : 0;
  if (frame) {
    graph.values[0].origin = OpResult{{0}};
    graph.ops.push_back({ValueFrameThis{},{0},0,{0},0,ValueId{0},LanguageEffect::kNone,LanguageEffect::kNone,ExceptionalCompletion::kImpossible,{0}});
    graph.origins.push_back({{0},{0},{}});
  }
  constexpr auto kEffects = LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto first = static_cast<std::uint32_t>(graph.uses.size());
    const OpId operation{i + prefix};
    graph.uses.push_back({operation,OperandRole::kBase,ReadPurpose::kOperand,ValueId{0},{},{},
        i ? std::optional{UseId{first-2}} : std::nullopt,
        i+1 < count ? std::optional{UseId{first+2}} : std::nullopt});
    graph.uses.push_back({operation,OperandRole::kStoredValue,ReadPurpose::kOperand,IntegerLiteral{42}});
    graph.ops.push_back({ValueOwnWrite{OwnWriteKind::kDefineField,{i},0},{0},operation.value,{first},2,{},kEffects,kEffects,ExceptionalCompletion::kMayThrow,{operation.value}});
    graph.origins.push_back({{operation.value},{operation.value},{}});
    graph.exceptional_exits.push_back({operation,{operation.value},kEffects});
  }
  const auto first = static_cast<std::uint32_t>(graph.uses.size());
  graph.uses.push_back({OpId{count + prefix},OperandRole::kReturnValue,ReadPurpose::kOperand,SpecialValue::kUndefined});
  graph.ops.push_back({ValueReturn{},{0},count + prefix,{first},1,{},LanguageEffect::kNone,LanguageEffect::kNone,ExceptionalCompletion::kImpossible,{count + prefix}});
  graph.origins.push_back({{count + prefix},{count + prefix},{}});
  graph.blocks.push_back({{0},count+prefix+1,{0},0}); return graph;
}
}
int main() {
  for (const bool frame : {false, true}) {
    std::uint64_t previous = 0;
    for (const auto count : {1U,2U,200U,201U,402U}) {
      const auto graph = Graph(count, frame);
      const auto built = LiteralClassFields::Build(graph);
      test::Check(std::holds_alternative<LiteralClassFields>(built));
      const auto& proof = std::get<LiteralClassFields>(built);
      test::Check(proof.Fields().size() == count);
      if (count == 402) test::Check(proof.Work() < 3*previous);
      previous = proof.Work();
      std::cout << "literal fields frame=" << frame << " count=" << count << " work=" << proof.Work() << '\n';
      SemanticBudget budget; budget.work = proof.Work();
      test::Check(std::holds_alternative<LiteralClassFields>(LiteralClassFields::Build(graph,budget)));
      --budget.work;
      test::Check(std::get<LiteralClassFieldError>(LiteralClassFields::Build(graph,budget)) == LiteralClassFieldError::kWorkLimit);
    }
  }
  auto frame = Graph(1, true);
  frame.values[0].domain = ValueKinds::Unknown();
  test::Check(std::get<LiteralClassFieldError>(LiteralClassFields::Build(frame)) == LiteralClassFieldError::kUnsupported);
  frame = Graph(1, true); frame.ops[0].operation = ValueFrameFunction{};
  test::Check(std::get<LiteralClassFieldError>(LiteralClassFields::Build(frame)) == LiteralClassFieldError::kUnsupported);
  frame = Graph(1, true); frame.uses[1].operand = ValueId{0};
  // A receiver read is an identity source, never a literal initializer RHS.
  frame.uses[0].next = UseId{1}; frame.uses[1].previous = UseId{0};
  test::Check(std::get<LiteralClassFieldError>(LiteralClassFields::Build(frame)) == LiteralClassFieldError::kUnsupported);
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
