#include "../../../src/source/evaluation_plan.h"

#include <cstdlib>

namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool condition) { if (!condition) std::abort(); }
class Fixture {
 public:
  Fixture() {
    graph.values.push_back({analysis::EntryDefinition{analysis::ParameterEntry{0}},
                             analysis::ValueKinds::Language(), {}});
  }
  analysis::OpId Add(analysis::ValueOperation operation, bool result,
      std::initializer_list<std::pair<analysis::OperandRole, analysis::ValueOperand>> operands,
      analysis::LanguageEffect effects = analysis::LanguageEffect::kNone) {
    return Add(operation, result, std::span(operands.begin(), operands.size()), effects);
  }
  analysis::OpId Add(analysis::ValueOperation operation, bool result,
      std::span<const std::pair<analysis::OperandRole, analysis::ValueOperand>> operands,
      analysis::LanguageEffect effects = analysis::LanguageEffect::kNone) {
    const analysis::OpId id{static_cast<std::uint32_t>(graph.ops.size())};
    std::optional<analysis::ValueId> value;
    if (result) {
      value = analysis::ValueId{static_cast<std::uint32_t>(graph.values.size())};
      graph.values.push_back({analysis::OpResult{id}, analysis::ValueKinds::Language(), {}});
    }
    graph.ops.push_back({operation, {0}, id.value,
        {static_cast<std::uint32_t>(graph.uses.size())}, static_cast<std::uint32_t>(operands.size()),
        value, effects, effects, effects == analysis::LanguageEffect::kNone
            ? analysis::ExceptionalCompletion::kImpossible : analysis::ExceptionalCompletion::kMayThrow,
        {id.value}});
    for (const auto& [role, operand] : operands) {
      const analysis::UseId use{static_cast<std::uint32_t>(graph.uses.size())};
      analysis::OperandUse occurrence{id, role, analysis::ReadPurpose::kOperand, operand, {}, {}, {}, {}};
      if (const auto* input = std::get_if<analysis::ValueId>(&operand)) {
        auto& definition = graph.values[input->value];
        occurrence.next = definition.first_use;
        if (occurrence.next) graph.uses[occurrence.next->value].previous = use;
        definition.first_use = use;
      }
      graph.uses.push_back(occurrence);
    }
    return id;
  }
  void Read() {
    Add(analysis::ValueNamedRead{{0}}, true, {{analysis::OperandRole::kBase, analysis::ValueId{0}}},
        analysis::LanguageEffect::kHeapRead | analysis::LanguageEffect::kUserCode);
  }
  void Write() {
    Add(analysis::ValueNamedWrite{{0}}, false,
        {{analysis::OperandRole::kBase, analysis::ValueId{0}},
         {analysis::OperandRole::kStoredValue, analysis::IntegerLiteral{0}}},
        analysis::LanguageEffect::kHeapWrite | analysis::LanguageEffect::kUserCode);
  }
  void Return(analysis::ValueId value) {
    Add(analysis::ValueReturn{}, false, {{analysis::OperandRole::kReturnValue, value}});
  }
  EvaluationPlan Plan() {
    graph.blocks = {{{0}, static_cast<std::uint32_t>(graph.ops.size()), {0}, 0}};
    auto built = BuildEvaluationPlan(graph);
    Check(std::holds_alternative<EvaluationPlan>(built));
    return std::get<EvaluationPlan>(std::move(built));
  }
  analysis::ValueProgram graph;
};

void OrderedOperands(bool reverse) {
  Fixture fixture;
  fixture.Read(); fixture.Read();
  fixture.Add(analysis::ValueBinary{analysis::BinaryOperator::kStrictEqual}, true,
      {{analysis::OperandRole::kLeft, analysis::ValueId{reverse ? 2U : 1U}},
       {analysis::OperandRole::kRight, analysis::ValueId{reverse ? 1U : 2U}}});
  fixture.Return({3});
  auto plan = fixture.Plan();
  Check(plan.operations[1].kind == EvaluationKind::kInline);
  Check(plan.operations[2].kind == EvaluationKind::kInline);
  Check(plan.operations[0].kind == (reverse ? EvaluationKind::kRoot : EvaluationKind::kInline));
  if (reverse) Check(plan.operations[0].temporary == TemporaryReason::kEffectSnapshot);
  Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(fixture.graph, plan)));
  ++plan.revision.value;
  Check(std::get<EvaluationError>(VerifyEvaluationPlan(fixture.graph, plan)) == EvaluationError::kRevision);
}
void Barrier(bool pure_parent) {
  Fixture fixture;
  fixture.Read();
  if (pure_parent) fixture.Add(analysis::ValueUnary{analysis::UnaryOperator::kTypeOf}, true,
      {{analysis::OperandRole::kInput, analysis::ValueId{1}}});
  fixture.Write();
  fixture.Return({pure_parent ? 2U : 1U});
  auto plan = fixture.Plan();
  const auto kept = pure_parent ? 1U : 0U;
  Check(plan.operations[kept].kind == EvaluationKind::kRoot);
  Check(plan.operations[kept].temporary == TemporaryReason::kEffectSnapshot);
  // Forge the very motion the planner refused. The verifier must discover the
  // changed effect sequence independently, including effects in a pure subtree.
  plan.operations[kept] = {EvaluationKind::kInline, fixture.graph.ops.back().first_use, {}};
  Check(std::get<EvaluationError>(VerifyEvaluationPlan(fixture.graph, plan)) == EvaluationError::kOrder);
}
void SharedValue() {
  Fixture fixture;
  fixture.Read();
  fixture.Add(analysis::ValueBinary{analysis::BinaryOperator::kStrictEqual}, true,
      {{analysis::OperandRole::kLeft, analysis::ValueId{1}},
       {analysis::OperandRole::kRight, analysis::ValueId{1}}});
  fixture.Return({2});
  const auto plan = fixture.Plan();
  Check(plan.operations[0].kind == EvaluationKind::kRoot);
  Check(plan.operations[0].temporary == TemporaryReason::kMultipleUses);
  analysis::SemanticBudget budget;
  budget.work = plan.work - 1;
  Check(std::get<EvaluationError>(BuildEvaluationPlan(fixture.graph, budget)) == EvaluationError::kWorkLimit);
  budget.work = plan.work;
  Check(std::holds_alternative<EvaluationPlan>(BuildEvaluationPlan(fixture.graph, budget)));
}
void CrossBlock() {
  Fixture fixture;
  fixture.Read();
  fixture.Add(analysis::ValueJump{}, false, {});
  fixture.Return({1});
  fixture.graph.blocks = {{{0}, 2, {0}, 1}, {{2}, 1, {1}, 0}};
  fixture.graph.edges = {{{0}, {1}, analysis::FlowEdgeKind::kTaken, 0}};
  fixture.graph.ops[2].block = {1}; fixture.graph.ops[2].order = 0;
  const auto built = BuildEvaluationPlan(fixture.graph);
  const auto* plan = std::get_if<EvaluationPlan>(&built);
  Check(plan && plan->operations[0].temporary == TemporaryReason::kCrossRegionValue);
}
void LongExpression() {
  for (const std::uint32_t count : {200U, 201U, 4096U}) {
    Fixture fixture;
    for (std::uint32_t i = 0; i < count; ++i)
      fixture.Add(analysis::ValueUnary{analysis::UnaryOperator::kLogicalNot}, true,
          {{analysis::OperandRole::kInput, analysis::ValueId{i}}});
    fixture.Return({count});
    const auto plan = fixture.Plan();
    for (std::uint32_t i = 0; i < count; ++i)
      Check(plan.operations[i].kind == EvaluationKind::kInline);
    Check(plan.operations.back().kind == EvaluationKind::kRoot);
    constexpr std::uint64_t kWorkPerOperationBound = 128;
    Check(plan.work <= kWorkPerOperationBound * fixture.graph.ops.size());
  }
}
void PredicateReads() {
  Fixture fixture;
  fixture.Read();
  fixture.Add(analysis::ValueUnary{analysis::UnaryOperator::kFalseOrZero}, true,
      {{analysis::OperandRole::kInput, analysis::ValueId{1}}});
  fixture.Return({2});
  auto plan = fixture.Plan();
  Check(plan.operations[0].temporary == TemporaryReason::kPredicateSnapshot);
  Check(plan.operations[1].kind == EvaluationKind::kInline);
  plan.operations[0] = {EvaluationKind::kInline, fixture.graph.ops[1].first_use, {}};
  Check(std::get<EvaluationError>(VerifyEvaluationPlan(fixture.graph, plan)) == EvaluationError::kOwnership);
}
void CallOrder(bool reverse, bool barrier) {
  Fixture fixture;
  fixture.Read(); fixture.Read();
  if (barrier) fixture.Write();
  const auto call = fixture.Add(analysis::ValueInvoke{analysis::InvokeReceiver::kUndefined, 1}, true,
      {{analysis::OperandRole::kCallee, analysis::ValueId{reverse ? 2U : 1U}},
       {analysis::OperandRole::kArgument, analysis::ValueId{reverse ? 1U : 2U}}},
      analysis::LanguageEffect::kUserCode);
  fixture.Return({3});
  auto plan = fixture.Plan();
  Check(plan.operations[call.value].kind == EvaluationKind::kInline);
  Check(plan.operations[0].kind == (reverse || barrier ? EvaluationKind::kRoot : EvaluationKind::kInline));
  Check(plan.operations[1].kind == (barrier ? EvaluationKind::kRoot : EvaluationKind::kInline));
  if (barrier) {
    const analysis::UseId callee{fixture.graph.ops[call.value].first_use.value};
    plan.operations[reverse ? 1 : 0] = {EvaluationKind::kInline, callee, {}};
    Check(std::get<EvaluationError>(VerifyEvaluationPlan(fixture.graph, plan)) == EvaluationError::kOrder);
  }
}
void SharedAndUnusedCalls() {
  Fixture fixture;
  fixture.Add(analysis::ValueInvoke{analysis::InvokeReceiver::kUndefined, 0}, true,
      {{analysis::OperandRole::kCallee, analysis::ValueId{0}}}, analysis::LanguageEffect::kUserCode);
  fixture.Add(analysis::ValueBinary{analysis::BinaryOperator::kStrictEqual}, true,
      {{analysis::OperandRole::kLeft, analysis::ValueId{1}},
       {analysis::OperandRole::kRight, analysis::ValueId{1}}});
  fixture.Return({2});
  const auto plan = fixture.Plan();
  Check(plan.operations[0].kind == EvaluationKind::kRoot &&
        plan.operations[0].temporary == TemporaryReason::kMultipleUses);
  Fixture unused;
  unused.Add(analysis::ValueConstruct{0}, true,
      {{analysis::OperandRole::kConstructor, analysis::ValueId{0}}}, analysis::LanguageEffect::kUserCode);
  unused.Return({0});
  const auto kept = unused.Plan();
  Check(kept.operations[0].kind == EvaluationKind::kRoot && !kept.operations[0].temporary);
}
void CallRanges() {
  for (const auto count : {200U, 201U, 4096U}) {
    Fixture fixture;
    using Operand = std::pair<analysis::OperandRole, analysis::ValueOperand>;
    std::vector<Operand> operands{{analysis::OperandRole::kCallee, analysis::ValueId{0}},
                                 {analysis::OperandRole::kReceiver, analysis::ValueId{0}}};
    operands.insert(operands.end(), count, {analysis::OperandRole::kArgument, analysis::ValueId{0}});
    fixture.Add(analysis::ValueInvoke{analysis::InvokeReceiver::kExplicit, count}, true,
                std::span<const Operand>(operands), analysis::LanguageEffect::kUserCode);
    fixture.Return({1});
    const auto plan = fixture.Plan();
    Check(plan.operations[0].kind == EvaluationKind::kInline);
    constexpr std::uint64_t kWorkPerUseBound = 128;
    Check(plan.work <= kWorkPerUseBound * fixture.graph.uses.size());
    fixture.graph.uses[2].role = analysis::OperandRole::kReceiver;
    Check(std::get<EvaluationError>(VerifyEvaluationPlan(fixture.graph, plan)) == EvaluationError::kInvalidGraph);
  }
}
void GlobalReads() {
  for (const auto lookup : {analysis::GlobalLookup::kName, analysis::GlobalLookup::kVar}) {
    Fixture fixture;
    fixture.Add(analysis::ValueGlobalRead{lookup, {0}}, true, {});
    fixture.Write();
    fixture.Return({1});
    const auto plan = fixture.Plan();
    Check(OrderedEvaluation(fixture.graph.ops[0]));
    Check(plan.operations[0].kind == EvaluationKind::kRoot);
    Check(plan.operations[0].temporary == TemporaryReason::kEffectSnapshot);
    auto forged = plan;
    forged.operations[0] = {EvaluationKind::kInline, fixture.graph.ops.back().first_use, {}};
    Check(std::get<EvaluationError>(VerifyEvaluationPlan(fixture.graph, forged)) == EvaluationError::kOrder);
    Fixture unused;
    unused.Add(analysis::ValueGlobalRead{lookup, {0}}, true, {});
    unused.Return({0});
    const auto kept = unused.Plan();
    Check(kept.operations[0].kind == EvaluationKind::kRoot && !kept.operations[0].temporary);
  }
}
}  // namespace

int main() {
  OrderedOperands(false); OrderedOperands(true);
  Barrier(false); Barrier(true);
  SharedValue(); CrossBlock();
  LongExpression();
  PredicateReads();
  CallOrder(false, false); CallOrder(true, false);
  CallOrder(false, true); CallOrder(true, true);
  SharedAndUnusedCalls(); CallRanges();
  GlobalReads();
  Fixture creation;
  creation.Add(analysis::ValueFunctionCreation{analysis::CreationKind::kFunction,
      {0}, 0, analysis::HomeObjectInput::kCurrentFunction, 0}, true, {});
  creation.graph.blocks = {{{0}, 1, {0}, 0}};
  Check(OrderedEvaluation(creation.graph.ops[0]));
  Check(std::get<EvaluationError>(BuildEvaluationPlan(creation.graph)) == EvaluationError::kOwnership);
  // Both are allocation leaves. Buffer syntax additionally requires resolved
  // properties and the independent tree operand/origin checks.
  Check(DescribeEvaluation(analysis::ValueObjectAllocation{}).has_value());
  Check(DescribeEvaluation(analysis::ValueObjectAllocation{abc::PoolIndex{0}}).has_value());
  for (const auto space : {analysis::ModuleSpace::kLocal, analysis::ModuleSpace::kExternal}) {
    const analysis::ValueModuleRead module{space, {0}};
    Check(!DescribeEvaluation(module)); // Enclosing module bindings are not yet proved.
    auto read = creation.graph.ops[0]; read.operation = module;
    Check(OrderedEvaluation(read));
  }
  const analysis::ValueOperation cells[]{analysis::ValueLexicalLoad{{{0}, {0}}},
      analysis::ValueLexicalStore{{{0}, {0}}}, analysis::ValueEnvironmentPush{1, {}},
      analysis::ValueEnvironmentPop{}, analysis::ValueCurrentEnvironment{}};
  for (const auto& operation : cells) {
    Check(DescribeEvaluation(operation).has_value() != IsEnvironmentState(operation));
    auto op = creation.graph.ops[0]; op.operation = operation;
    Check(OrderedEvaluation(op));
  }
}
