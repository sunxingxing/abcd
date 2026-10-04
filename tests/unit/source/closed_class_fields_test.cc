#include "../../../src/source/closed_class_fields.h"
#include "analysis/ssa_fixture.h"
#include "../../../src/source/effect_order.h"
#include "../../../src/source/evaluation_plan.h"
#include "../../../src/source/closed_values.h"
#include <algorithm>

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
    graph.uses.push_back({operation,OperandRole::kStoredValue,ReadPurpose::kOperand,analysis::IntegerLiteral{42}});
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
      {{OperandRole::kBase, ValueId{0}}, {OperandRole::kStoredValue, analysis::IntegerLiteral{static_cast<std::int32_t>(order)}}},
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
ValueProgram CopiedLiteral(std::uint32_t count, bool reverse, bool string) {
  ValueProgram graph;
  graph.values.push_back({EntryDefinition{ReceiverEntry{}}, ValueKinds::Language(), {}});
  for (std::uint32_t i = 0; i <= count; ++i)
    graph.values.push_back({EntryDefinition{UnknownEntry{}}, string ? ValueKinds::String() : ValueKinds::Number(), {}});
  const auto add = [&](ValueOperation operation, std::optional<ValueId> result,
      std::initializer_list<std::pair<OperandRole, ValueOperand>> inputs, std::optional<std::uint32_t> next) {
    const WorkBlockId block{static_cast<std::uint32_t>(graph.blocks.size())};
    const OpId id{static_cast<std::uint32_t>(graph.ops.size())};
    const auto first = static_cast<std::uint32_t>(graph.uses.size());
    const bool field = std::holds_alternative<ValueOwnWrite>(operation);
    const auto effects = field ? LanguageEffect::kHeapWrite | LanguageEffect::kUserCode : LanguageEffect::kNone;
    graph.ops.push_back({operation, block, 0, {first}, static_cast<std::uint32_t>(inputs.size()), result,
      effects, effects, field ? ExceptionalCompletion::kMayThrow : ExceptionalCompletion::kImpossible, {id.value}});
    graph.origins.push_back({{id.value}, {id.value}, {}});
    if (result) graph.values[result->value].origin = OpResult{id};
    for (const auto& [role, operand] : inputs) {
      const UseId at{static_cast<std::uint32_t>(graph.uses.size())};
      OperandUse use{id, role, ReadPurpose::kOperand, operand};
      if (const auto* value = std::get_if<ValueId>(&operand)) {
        use.next = graph.values[value->value].first_use;
        if (use.next) graph.uses[use.next->value].previous = at;
        graph.values[value->value].first_use = at;
      }
      graph.uses.push_back(use);
    }
    graph.blocks.push_back({id, 1, {static_cast<std::uint32_t>(graph.edges.size())}, static_cast<std::uint8_t>(next ? 1 : 0)});
    if (next) graph.edges.push_back({block, {*next}, FlowEdgeKind::kFallthrough, 0});
    if (field) graph.exceptional_exits.push_back({id, {id.value}, effects});
  };
  add(ValueNop{}, {}, {}, reverse ? count+3 : 1);
  for (std::uint32_t i = 0; i < count+3; ++i) {
    const auto position = reverse ? count+2-i : i;
    const auto block = static_cast<std::uint32_t>(graph.blocks.size());
    const std::optional<std::uint32_t> next = position == count+2 ? std::nullopt :
      std::optional<std::uint32_t>{reverse ? block-1 : block+1};
    if (!position) {
      if (string) add(ValueString{{0}}, ValueId{1}, {}, next);
      else add(ValueCopy{}, ValueId{1}, {{OperandRole::kInput, analysis::IntegerLiteral{42}}}, next);
    } else if (position <= count) add(ValueCopy{}, ValueId{position+1},
      {{OperandRole::kInput, ValueId{position}}}, next);
    else if (position == count+1) add(ValueOwnWrite{OwnWriteKind::kDefineField, {0}, 0}, {},
      {{OperandRole::kBase, ValueId{0}}, {OperandRole::kStoredValue, ValueId{count+1}}}, next);
    else add(ValueReturn{}, {}, {{OperandRole::kReturnValue, SpecialValue::kUndefined}}, {});
  }
  return graph;
}
ValueProgram SelectedValue(bool identity) {
  ValueProgram graph;
  graph.values = {{EntryDefinition{ReceiverEntry{}},ValueKinds::Language(),{}},
      {EntryDefinition{ParameterEntry{0}},ValueKinds::ZeroOrOnePrimitive(),{}},
      {OpResult{{0}},ValueKinds::String(),{}}, {OpResult{{2}},ValueKinds::String(),{}},
      {OpResult{{4}},identity ? ValueKinds::String() : ValueKinds::Number(),{}},
      {PhiDefinition{{3}},ValueKinds::Language(),{}}};
  graph.blocks = {{{0},2,{0},2},{{2},2,{2},1},{{4},2,{3},1},{{6},2,{4},0}};
  graph.edges = {{{0},{1},FlowEdgeKind::kTaken,0},{{0},{2},FlowEdgeKind::kFallthrough,1},
      {{1},{3},FlowEdgeKind::kTaken,0},{{2},{3},FlowEdgeKind::kTaken,0}};
  const auto pure = LanguageEffect::kNone;
  const auto normal = ExceptionalCompletion::kImpossible;
  constexpr auto effects = LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
  graph.ops = {
      {ValueString{{0}},{0},0,{0},0,ValueId{2},pure,pure,normal,{0}},
      {ValueBranch{UnaryOperator::kToBoolean},{0},1,{0},1,{},pure,pure,normal,{1}},
      {ValueCopy{},{1},0,{1},1,ValueId{3},pure,pure,normal,{2}},
      {ValueJump{},{1},1,{2},0,{},pure,pure,normal,{3}},
      {ValueCopy{},{2},0,{2},1,ValueId{4},pure,pure,normal,{4}},
      {ValueJump{},{2},1,{3},0,{},pure,pure,normal,{5}},
      {ValueOwnWrite{OwnWriteKind::kDefineField,{0},0},{3},0,{3},2,{},effects,effects,ExceptionalCompletion::kMayThrow,{6}},
      {ValueReturn{},{3},1,{5},1,{},pure,pure,normal,{7}}};
  graph.uses = {{OpId{1},OperandRole::kCondition,ReadPurpose::kOperand,ValueId{1}},
      {OpId{2},OperandRole::kInput,ReadPurpose::kOperand,ValueId{2}},
      {OpId{4},OperandRole::kInput,ReadPurpose::kOperand,identity ? ValueOperand{ValueId{2}} : ValueOperand{analysis::IntegerLiteral{42}}},
      {OpId{6},OperandRole::kBase,ReadPurpose::kOperand,ValueId{0}},
      {OpId{6},OperandRole::kStoredValue,ReadPurpose::kOperand,ValueId{5}},
      {OpId{7},OperandRole::kReturnValue,ReadPurpose::kOperand,SpecialValue::kUndefined},
      {ValueId{5},OperandRole::kPhiInput,ReadPurpose::kOperand,ValueId{3},EdgeId{2}},
      {ValueId{5},OperandRole::kPhiInput,ReadPurpose::kOperand,ValueId{4},EdgeId{3}}};
  for (std::uint32_t i = 0; i < graph.uses.size(); ++i) {
    auto& use = graph.uses[i];
    if (const auto* value = std::get_if<ValueId>(&use.operand)) {
      use.next = graph.values[value->value].first_use;
      if (use.next) graph.uses[use.next->value].previous = UseId{i};
      graph.values[value->value].first_use = UseId{i};
    }
  }
  for (std::uint32_t i = 0; i < graph.ops.size(); ++i) graph.origins.push_back({{i},{i},{}});
  graph.exceptional_exits.push_back({{6},{6},effects});
  return graph;
}
void Relink(ValueProgram& graph) {
  for (auto& value : graph.values) value.first_use.reset();
  for (auto& use : graph.uses) { use.previous.reset(); use.next.reset(); }
  for (std::uint32_t i = 0; i < graph.uses.size(); ++i) {
    auto& use = graph.uses[i];
    if (const auto* value = std::get_if<ValueId>(&use.operand)) {
      use.next = graph.values[value->value].first_use;
      if (use.next) graph.uses[use.next->value].previous = UseId{i};
      graph.values[value->value].first_use = UseId{i};
    }
  }
}
}
int main() {
  for (const auto domain : {ValueKinds::Number(),ValueKinds::String(),ValueKinds::Language()}) {
    auto graph = SelectedValue(true);
    graph.values[1].domain = domain;
    graph.uses[4].operand = ValueId{1}; Relink(graph);
    test::Check(std::holds_alternative<EffectOrder>(BuildEffectOrder(graph)));
    const auto facts = std::get<ClosedValues>(ClosedValues::Build(graph));
    test::Check(!facts.Contains(graph,ValueOperand{ValueId{1}}));
    test::Check(std::holds_alternative<ClosedClassFieldError>(ClosedClassFields::Build(graph)));
  }
  for (const ValueOperation operation : {ValueOperation{ValueGlobalRead{GlobalLookup::kName,{0}}},
      ValueOperation{ValueLexicalLoad{{{0},{0}}}},ValueOperation{ValueFrameThis{}}}) {
    auto graph = CopiedLiteral(201,true,true);
    const auto source = std::get<OpResult>(graph.values[1].origin).op;
    graph.ops[source.value].operation = operation;
    // Even forged zero effects cannot turn an implicit context read into a
    // closed leaf. Use the real common shape/effect owner, not a local list.
    test::Check(std::holds_alternative<EffectOrder>(BuildEffectOrder(graph)));
    test::Check(std::holds_alternative<ClosedClassFieldError>(ClosedClassFields::Build(graph)));
  }
  {
    auto graph = CopiedLiteral(2,true,true);
    const auto unary = std::get<OpResult>(graph.values[2].origin).op;
    graph.ops[unary.value].operation = ValueUnary{UnaryOperator::kTypeOf};
    const auto binary = std::get<OpResult>(graph.values[3].origin).op;
    auto& op = graph.ops[binary.value]; op.operation = ValueBinary{BinaryOperator::kStrictEqual};
    const auto at = op.first_use.value;
    graph.uses[at].role = OperandRole::kLeft;
    graph.uses.insert(graph.uses.begin()+at+1,{binary,OperandRole::kRight,ReadPurpose::kOperand,ValueId{2}});
    ++op.use_count; graph.values[3].domain = ValueKinds::ZeroOrOnePrimitive();
    for (auto& other : graph.ops) if (other.first_use.value > at) ++other.first_use.value;
    Relink(graph);
    test::Check(std::holds_alternative<EffectOrder>(BuildEffectOrder(graph)));
    test::Check(std::holds_alternative<ClosedClassFields>(ClosedClassFields::Build(graph)));
  }
  for (const bool identity : {false,true}) {
    const auto graph = SelectedValue(identity);
    test::Check(std::holds_alternative<EffectOrder>(BuildEffectOrder(graph)));
    const auto facts = std::get<ClosedValues>(ClosedValues::Build(graph));
    test::Check(facts.Contains(graph,ValueOperand{ValueId{5}}) == identity);
    test::Check(!facts.Contains(graph,ValueOperand{ValueId{0}}));
    test::Check(!facts.Contains(graph,ValueOperand{ValueId{1}}));
    test::Check(std::holds_alternative<ClosedClassFields>(ClosedClassFields::Build(graph)) == identity);
  }
  for (const bool reverse : {false,true}) {
    std::uint64_t previous = 0;
    for (const auto count : {1U,200U,201U,402U}) {
      auto graph = CopiedLiteral(count,reverse,false);
      for (std::uint32_t i = 2; i < graph.values.size(); ++i) {
        const auto op = std::get<OpResult>(graph.values[i].origin).op;
        graph.ops[op.value].operation = ValueUnary{i == 2 ? UnaryOperator::kTypeOf : UnaryOperator::kLogicalNot};
        graph.values[i].domain = i == 2 ? ValueKinds::String() : ValueKinds::ZeroOrOnePrimitive();
      }
      const auto checked = ClosedClassFields::Build(graph);
      test::Check(std::holds_alternative<ClosedClassFields>(checked));
      const auto& proof = std::get<ClosedClassFields>(checked);
      if (count == 402) test::Check(proof.Work() < 3*previous);
      previous = proof.Work();
      std::cout << "closed composition reverse=" << reverse << " count=" << count << " work=" << proof.Work() << '\n';
      const auto source = std::get<OpResult>(graph.values[1].origin).op;
      graph.ops[source.value].operation = ValueFrameThis{};
      test::Check(std::holds_alternative<ClosedClassFieldError>(ClosedClassFields::Build(graph)));
    }
  }
  {
    auto graph = CopiedLiteral(201, true, true);
    const auto source = std::get<OpResult>(graph.values[1].origin).op;
    const auto built = BuildEvaluationPlan(graph);
    test::Check(std::holds_alternative<EvaluationPlan>(built));
    const auto& plan = std::get<EvaluationPlan>(built);
    test::Check(plan.closed_expressions[source.value].has_value());
    test::Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(graph, plan)));
    auto changed = plan;
    changed.closed_expressions[source.value].reset();
    test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, changed)));
    changed = plan;
    const auto field = std::find_if(graph.ops.begin(), graph.ops.end(), [](const ValueOp& op) {
      return std::holds_alternative<ValueOwnWrite>(op.operation);
    });
    test::Check(field != graph.ops.end() && field->first_use != changed.closed_expressions[source.value]);
    changed.closed_expressions[source.value] = field->first_use;
    changed.operations[source.value].consumer = changed.closed_expressions[source.value];
    test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, changed)));
    graph.ops[source.value].normal = LanguageEffect::kHeapRead;
    test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, plan)));
    test::Check(std::holds_alternative<ClosedClassFieldError>(ClosedClassFields::Build(graph)));
    graph.ops[source.value].normal = LanguageEffect::kNone;
    graph.ops[source.value].operation = ValueFrameThis{};
    // Even a no-throw frame read depends on its original implicit context.
    test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, plan)));
    test::Check(std::holds_alternative<ClosedClassFieldError>(ClosedClassFields::Build(graph)));
    graph.ops[source.value].operation = ValueString{{0}};
    graph.values[1].domain = ValueKinds::Unknown();
    test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, plan)));
    test::Check(std::holds_alternative<ClosedClassFieldError>(ClosedClassFields::Build(graph)));
  }
  for (const bool string : {false, true}) for (const bool reverse : {false, true}) {
    std::uint64_t previous = 0;
    for (const auto count : {1U, 200U, 201U, 402U}) {
      const auto graph = CopiedLiteral(count, reverse, string);
      test::Check(std::holds_alternative<EffectOrder>(BuildEffectOrder(graph)));
      const auto built = ClosedClassFields::Build(graph);
      if (const auto* error = std::get_if<ClosedClassFieldError>(&built))
        std::cerr << "literal copy reverse=" << reverse << " string=" << string << " error=" << static_cast<int>(*error) << '\n';
      test::Check(std::holds_alternative<ClosedClassFields>(built));
      const auto& proof = std::get<ClosedClassFields>(built);
      test::Check(proof.Fields().size() == 1);
      SemanticBudget budget; budget.work = proof.Work();
      test::Check(std::holds_alternative<ClosedClassFields>(ClosedClassFields::Build(graph, budget)));
      --budget.work;
      test::Check(std::get<ClosedClassFieldError>(ClosedClassFields::Build(graph, budget)) == ClosedClassFieldError::kWorkLimit);
      if (count == 402) test::Check(proof.Work() < 3*previous);
      previous = proof.Work();
      std::cout << "literal copy reverse=" << reverse << " string=" << string << " count=" << count << " work=" << proof.Work() << '\n';
    }
  }
  for (const auto flow : {FieldFlow::kEmptyEntry, FieldFlow::kSplit, FieldFlow::kReverse, FieldFlow::kDiamond, FieldFlow::kExitFork}) {
    std::uint64_t previous = 0;
    for (const auto count : {1U, 200U, 201U, 402U}) {
      const auto graph = AcrossBlocks(count, flow);
      test::Check(std::holds_alternative<EffectOrder>(BuildEffectOrder(graph)));
      const auto built = ClosedClassFields::Build(graph);
      if (const auto* error = std::get_if<ClosedClassFieldError>(&built))
        std::cerr << "field flow=" << static_cast<int>(flow) << " error=" << static_cast<int>(*error) << '\n';
      test::Check(std::holds_alternative<ClosedClassFields>(built));
      const auto& proof = std::get<ClosedClassFields>(built);
      test::Check(proof.Fields().size() == count);
      for (std::size_t i = 0; i < count; ++i)
        test::Check(std::get<ValueOwnWrite>(graph.ops[proof.Fields()[i].value].operation).name.value == i);
      SemanticBudget budget; budget.work = proof.Work();
      test::Check(std::holds_alternative<ClosedClassFields>(ClosedClassFields::Build(graph, budget)));
      --budget.work;
      test::Check(std::get<ClosedClassFieldError>(ClosedClassFields::Build(graph, budget)) == ClosedClassFieldError::kWorkLimit);
      if (count == 402) test::Check(proof.Work() < 3*previous);
      previous = proof.Work();
      std::cout << "field flow=" << static_cast<int>(flow) << " count=" << count << " work=" << proof.Work() << '\n';
    }
  }
  for (const auto flow : {FieldFlow::kBypass, FieldFlow::kPrefixLoop, FieldFlow::kSuffixLoop, FieldFlow::kRepeatedField, FieldFlow::kBarrier}) {
    const auto graph = AcrossBlocks(2, flow);
    test::Check(std::holds_alternative<EffectOrder>(BuildEffectOrder(graph)));
    test::Check(std::get<ClosedClassFieldError>(ClosedClassFields::Build(graph)) == ClosedClassFieldError::kUnsupported);
  }
  for (const bool frame : {false, true}) {
    std::uint64_t previous = 0;
    for (const auto count : {1U,2U,200U,201U,402U}) {
      const auto graph = Graph(count, frame);
      const auto built = ClosedClassFields::Build(graph);
      test::Check(std::holds_alternative<ClosedClassFields>(built));
      const auto& proof = std::get<ClosedClassFields>(built);
      test::Check(proof.Fields().size() == count);
      if (count == 402) test::Check(proof.Work() < 3*previous);
      previous = proof.Work();
      std::cout << "literal fields frame=" << frame << " count=" << count << " work=" << proof.Work() << '\n';
      SemanticBudget budget; budget.work = proof.Work();
      test::Check(std::holds_alternative<ClosedClassFields>(ClosedClassFields::Build(graph,budget)));
      --budget.work;
      test::Check(std::get<ClosedClassFieldError>(ClosedClassFields::Build(graph,budget)) == ClosedClassFieldError::kWorkLimit);
    }
  }
  auto frame = Graph(1, true);
  frame.values[0].domain = ValueKinds::Unknown();
  test::Check(std::get<ClosedClassFieldError>(ClosedClassFields::Build(frame)) == ClosedClassFieldError::kUnsupported);
  frame = Graph(1, true); frame.ops[0].operation = ValueFrameFunction{};
  test::Check(std::get<ClosedClassFieldError>(ClosedClassFields::Build(frame)) == ClosedClassFieldError::kUnsupported);
  frame = Graph(1, true); frame.uses[1].operand = ValueId{0};
  // A receiver read is an identity source, never a literal initializer RHS.
  frame.uses[0].next = UseId{1}; frame.uses[1].previous = UseId{0};
  test::Check(std::get<ClosedClassFieldError>(ClosedClassFields::Build(frame)) == ClosedClassFieldError::kUnsupported);
  auto graph = Graph(1);
  graph.values[0].origin = EntryDefinition{ParameterEntry{0}};
  test::Check(std::get<ClosedClassFieldError>(ClosedClassFields::Build(graph)) == ClosedClassFieldError::kUnsupported);
  graph = Graph(1); graph.uses[2].operand = analysis::IntegerLiteral{1};
  test::Check(std::get<ClosedClassFieldError>(ClosedClassFields::Build(graph)) == ClosedClassFieldError::kUnsupported);
  graph = Graph(1); std::get<ValueOwnWrite>(graph.ops[0].operation).kind = OwnWriteKind::kStore;
  test::Check(std::get<ClosedClassFieldError>(ClosedClassFields::Build(graph)) == ClosedClassFieldError::kUnsupported);
  graph = Graph(1); graph.ops[0].operation = ValueNamedWrite{{0}};
  test::Check(std::get<ClosedClassFieldError>(ClosedClassFields::Build(graph)) == ClosedClassFieldError::kUnsupported);
  graph = Graph(1); graph.ops[0].normal = LanguageEffect::kNone;
  test::Check(std::holds_alternative<ClosedClassFieldError>(ClosedClassFields::Build(graph)));
  graph = Graph(1); graph.uses[1].operand = Float64Bits{0xffff000000000005ULL};
  test::Check(std::get<ClosedClassFieldError>(ClosedClassFields::Build(graph)) == ClosedClassFieldError::kUnsupported);
  test::Check(std::get<ClosedClassFieldError>(ClosedClassFields::Build(Graph(1),{0,1000})) == ClosedClassFieldError::kResourceLimit);
}
