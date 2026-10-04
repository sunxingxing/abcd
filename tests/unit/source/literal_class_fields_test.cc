#include "../../../src/source/literal_class_fields.h"
#include "analysis/ssa_fixture.h"
#include "../../../src/source/effect_order.h"

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
enum class FieldFlow { kSplit, kReverse, kDiamond, kExitFork, kBypass, kPrefixLoop, kSuffixLoop, kRepeatedField, kBarrier, kEmptyEntry };
ValueProgram AcrossBlocks(std::uint32_t count, FieldFlow flow) {
  ValueProgram graph;
  graph.values.push_back({EntryDefinition{ReceiverEntry{}}, ValueKinds::Language(), {}});
  graph.values.push_back({EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}});
  const bool prefix_fork = flow == FieldFlow::kDiamond || flow == FieldFlow::kBypass || flow == FieldFlow::kPrefixLoop;
  const bool suffix_fork = flow == FieldFlow::kExitFork || flow == FieldFlow::kSuffixLoop || flow == FieldFlow::kRepeatedField;
  const std::uint32_t first_field = prefix_fork ? 3 : 1, tail = first_field + count;
  constexpr auto kEffects = LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
  const auto add = [&](ValueOperation operation, std::initializer_list<std::pair<OperandRole, ValueOperand>> uses,
      std::initializer_list<std::pair<std::uint32_t, FlowEdgeKind>> edges, LanguageEffect effect = LanguageEffect::kNone) {
    const WorkBlockId block{static_cast<std::uint32_t>(graph.blocks.size())};
    const OpId id{static_cast<std::uint32_t>(graph.ops.size())};
    const auto first_use = static_cast<std::uint32_t>(graph.uses.size());
    graph.ops.push_back({operation, block, 0, {first_use}, static_cast<std::uint32_t>(uses.size()), {},
      effect, effect, effect == LanguageEffect::kNone ? ExceptionalCompletion::kImpossible : ExceptionalCompletion::kMayThrow, {id.value}});
    graph.origins.push_back({{id.value}, {id.value}, {}});
    for (const auto& [role, operand] : uses) {
      const UseId at{static_cast<std::uint32_t>(graph.uses.size())};
      OperandUse use{id, role, ReadPurpose::kOperand, operand};
      if (const auto* value = std::get_if<ValueId>(&operand)) {
        use.next = graph.values[value->value].first_use;
        if (use.next) graph.uses[use.next->value].previous = at;
        graph.values[value->value].first_use = at;
      }
      graph.uses.push_back(use);
    }
    graph.blocks.push_back({id, 1, {static_cast<std::uint32_t>(graph.edges.size())}, static_cast<std::uint8_t>(edges.size())});
    std::uint8_t ordinal = 0;
    for (const auto& [target, kind] : edges) graph.edges.push_back({block, {target}, kind, ordinal++});
    if (effect != LanguageEffect::kNone) graph.exceptional_exits.push_back({id, {id.value}, effect});
  };
  const auto branch = ValueBranch{UnaryOperator::kTrueOrNonzeroNumber};
  if (prefix_fork) {
    add(branch, {{OperandRole::kCondition, ValueId{1}}}, {{1, FlowEdgeKind::kTaken}, {2, FlowEdgeKind::kFallthrough}});
    add(ValueNop{}, {}, {{flow == FieldFlow::kBypass ? tail : flow == FieldFlow::kPrefixLoop ? 1 : first_field, FlowEdgeKind::kFallthrough}});
    add(ValueNop{}, {}, {{first_field, FlowEdgeKind::kFallthrough}});
  } else add(ValueNop{}, {}, {{flow == FieldFlow::kReverse || flow == FieldFlow::kEmptyEntry ? tail-1 : first_field, FlowEdgeKind::kFallthrough}});
  for (std::uint32_t i = 0; i < count; ++i) {
    const bool reverse = flow == FieldFlow::kReverse || flow == FieldFlow::kEmptyEntry;
    const auto order = reverse ? count-i-1 : i;
    const auto next = reverse ? (i ? first_field+i-1 : tail) : first_field+i+1;
    add(ValueOwnWrite{OwnWriteKind::kDefineField, {order}, 0},
      {{OperandRole::kBase, ValueId{0}}, {OperandRole::kStoredValue, IntegerLiteral{static_cast<std::int32_t>(order)}}},
      {{next, FlowEdgeKind::kFallthrough}}, kEffects);
  }
  if (suffix_fork) {
    add(branch, {{OperandRole::kCondition, ValueId{1}}}, {{tail+1, FlowEdgeKind::kTaken},
      {flow == FieldFlow::kRepeatedField ? first_field : tail+2, FlowEdgeKind::kFallthrough}});
    add(ValueReturn{}, {{OperandRole::kReturnValue, SpecialValue::kUndefined}}, {});
    if (flow != FieldFlow::kRepeatedField) {
      if (flow == FieldFlow::kSuffixLoop) add(ValueNop{}, {}, {{tail+2, FlowEdgeKind::kFallthrough}});
      else add(ValueReturn{}, {{OperandRole::kReturnValue, SpecialValue::kUndefined}}, {});
    }
  } else {
    if (flow == FieldFlow::kBarrier) add(ValueDebugger{}, {}, {{tail+1, FlowEdgeKind::kFallthrough}});
    add(ValueReturn{}, {{OperandRole::kReturnValue, SpecialValue::kUndefined}}, {});
  }
  if (flow == FieldFlow::kEmptyEntry) {
    graph.ops.erase(graph.ops.begin()); graph.origins.erase(graph.origins.begin());
    graph.blocks[0].op_count = 0;
    for (std::size_t i = 1; i < graph.blocks.size(); ++i) --graph.blocks[i].first_op.value;
    for (auto& op : graph.ops) --op.origin.value;
    for (auto& use : graph.uses) --std::get<OpId>(use.owner).value;
    for (auto& exit : graph.exceptional_exits) { --exit.op.value; --exit.origin.value; }
  }
  return graph;
}
}
int main() {
  for (const auto flow : {FieldFlow::kEmptyEntry, FieldFlow::kSplit, FieldFlow::kReverse, FieldFlow::kDiamond, FieldFlow::kExitFork}) {
    std::uint64_t previous = 0;
    for (const auto count : {1U, 200U, 201U, 402U}) {
      const auto graph = AcrossBlocks(count, flow);
      test::Check(std::holds_alternative<EffectOrder>(BuildEffectOrder(graph)));
      const auto built = LiteralClassFields::Build(graph);
      if (const auto* error = std::get_if<LiteralClassFieldError>(&built))
        std::cerr << "field flow=" << static_cast<int>(flow) << " error=" << static_cast<int>(*error) << '\n';
      test::Check(std::holds_alternative<LiteralClassFields>(built));
      const auto& proof = std::get<LiteralClassFields>(built);
      test::Check(proof.Fields().size() == count);
      for (std::size_t i = 0; i < count; ++i)
        test::Check(std::get<ValueOwnWrite>(graph.ops[proof.Fields()[i].value].operation).name.value == i);
      SemanticBudget budget; budget.work = proof.Work();
      test::Check(std::holds_alternative<LiteralClassFields>(LiteralClassFields::Build(graph, budget)));
      --budget.work;
      test::Check(std::get<LiteralClassFieldError>(LiteralClassFields::Build(graph, budget)) == LiteralClassFieldError::kWorkLimit);
      if (count == 402) test::Check(proof.Work() < 3*previous);
      previous = proof.Work();
      std::cout << "field flow=" << static_cast<int>(flow) << " count=" << count << " work=" << proof.Work() << '\n';
    }
  }
  for (const auto flow : {FieldFlow::kBypass, FieldFlow::kPrefixLoop, FieldFlow::kSuffixLoop, FieldFlow::kRepeatedField, FieldFlow::kBarrier}) {
    const auto graph = AcrossBlocks(2, flow);
    test::Check(std::holds_alternative<EffectOrder>(BuildEffectOrder(graph)));
    test::Check(std::get<LiteralClassFieldError>(LiteralClassFields::Build(graph)) == LiteralClassFieldError::kUnsupported);
  }
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
