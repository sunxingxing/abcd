#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "abcd/source/tree_printer.h"
#include <sstream>
#include <algorithm>
#include "../../../src/analysis/value_edits.h"
#include "../../../src/source/array_initialization.h"
#include <iostream>
#include <cstdlib>
#include <source_location>
using namespace abcd::analysis;
using namespace abcd::source;
void Check(bool ok, const std::source_location& site=std::source_location::current()) {
  if (!ok) { std::cerr << site.line() << "\n"; std::abort(); }
}
class Fixture {
 public:
  void Add(ValueOperation operation, bool result, std::initializer_list<std::pair<OperandRole, ValueOperand>> inputs) {
    const OpId id{static_cast<std::uint32_t>(graph.ops.size())};
    std::optional<ValueId> output;
    if (result) {
      output = ValueId{static_cast<std::uint32_t>(graph.values.size())};
      graph.values.push_back({OpResult{id}, ValueKinds::Language(), {}});
    }
    graph.origins.push_back({{id.value}, {id.value}, {}});
    graph.ops.push_back({operation, {0}, id.value, {static_cast<std::uint32_t>(graph.uses.size())},
        static_cast<std::uint32_t>(inputs.size()), output, LanguageEffect::kNone,
        LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {id.value}});
    for (const auto& [role, operand] : inputs) {
      const UseId use{static_cast<std::uint32_t>(graph.uses.size())};
      OperandUse occurrence{id, role, ReadPurpose::kOperand, operand, {}, {}, {}, {}};
      if (const auto* value = std::get_if<ValueId>(&operand)) {
        auto& definition = graph.values[value->value];
        occurrence.next = definition.first_use;
        if (occurrence.next) graph.uses[occurrence.next->value].previous = use;
        definition.first_use = use;
      }
      graph.uses.push_back(occurrence);
    }
  }
  ValueProgram graph;
};

struct Sample {
  ValueProgram graph;
  std::vector<ExpressionConstant> constants;
  std::vector<std::u16string> names{u"length"};
  ExpressionConstants Constants() const { return {constants, names.size()}; }
};
Sample Make(unsigned count, bool copies=false, bool split=false, bool expose=false, bool late=false) {
  Fixture f;
  f.graph.values.push_back({EntryDefinition{ParameterEntry{0}},ValueKinds::Language(),{}});
  f.Add(ValueArrayAllocation{},true,{}); const auto array=*f.graph.ops.back().result;
  f.graph.ops.back().normal=LanguageEffect::kAllocation;
  ValueOperand cursor=abcd::analysis::IntegerLiteral{0};
  for (unsigned i=0;i<count;++i) {
    auto destination=array;
    if(copies) { f.Add(ValueCopy{},true,{{OperandRole::kInput,array}});destination=*f.graph.ops.back().result; }
    ValueOperand source = ValueId{0};
    if (late) {
      f.Add(ValueInvoke{InvokeReceiver::kUndefined, 0}, true, {{OperandRole::kCallee, ValueId{0}}});
      f.graph.ops.back().normal = f.graph.ops.back().before_throw = LanguageEffect::kUserCode | LanguageEffect::kHeapRead | LanguageEffect::kHeapWrite;
      f.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
      source = *f.graph.ops.back().result;
    }
    f.Add(ValueArraySpread{},true,{{OperandRole::kDestination,destination},{OperandRole::kIndex,cursor},{OperandRole::kSource,source}});
    auto& spread=f.graph.ops.back();spread.normal=spread.before_throw=LanguageEffect::kHeapRead|LanguageEffect::kHeapWrite|LanguageEffect::kUserCode|LanguageEffect::kAllocation;
    spread.exceptional=ExceptionalCompletion::kMayThrow;
    cursor=*spread.result;
    if(copies) { f.Add(ValueCopy{},true,{{OperandRole::kInput,cursor}});cursor=*f.graph.ops.back().result; }
    // Reconstruct current cursor via the same private destination's length.
    f.Add(ValueNamedRead{{0}},true,{{OperandRole::kBase,destination}});
    f.graph.ops.back().normal=f.graph.ops.back().before_throw=LanguageEffect::kHeapRead|LanguageEffect::kUserCode;
    f.graph.ops.back().exceptional=ExceptionalCompletion::kMayThrow;
    cursor=*f.graph.ops.back().result;
    ValueOperand element = abcd::analysis::IntegerLiteral{7};
    if (late) {
      f.Add(ValueInvoke{InvokeReceiver::kUndefined, 0}, true, {{OperandRole::kCallee, ValueId{0}}});
      f.graph.ops.back().normal = f.graph.ops.back().before_throw = LanguageEffect::kUserCode | LanguageEffect::kHeapRead | LanguageEffect::kHeapWrite;
      f.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
      element = *f.graph.ops.back().result;
    }
    f.Add(ValueComputedOwnWrite{},false,{{OperandRole::kBase,destination},{OperandRole::kKey,cursor},{OperandRole::kStoredValue,element}});
    f.graph.ops.back().normal=LanguageEffect::kHeapWrite;
    f.Add(ValueUnary{UnaryOperator::kIncrement},true,{{OperandRole::kInput,cursor}});
    cursor=*f.graph.ops.back().result;
  }
  f.Add(ValueReturn{},false,{{OperandRole::kReturnValue,expose?cursor:ValueOperand{array}}});
  f.graph.blocks={{{0},static_cast<unsigned>(f.graph.ops.size()),{0},0}};
  if(split) {
    // Every operation gets its own fallthrough block: no pattern or same-block proof.
    f.graph.blocks.clear();
    for(unsigned i=0;i<f.graph.ops.size();++i) {
      const bool last=i+1==f.graph.ops.size();
      f.graph.ops[i].block={i};f.graph.ops[i].order=0;
      f.graph.blocks.push_back({{i},1,{i},static_cast<std::uint8_t>(last?0:1)});
      if(!last) f.graph.edges.push_back({{i},{i+1},FlowEdgeKind::kFallthrough,0});
    }
  }
  Sample sample{std::move(f.graph)};sample.constants.resize(sample.graph.ops.size());
  for(unsigned i=0;i<sample.graph.ops.size();++i)
    if(std::holds_alternative<ValueNamedRead>(sample.graph.ops[i].operation)) sample.constants[i]=NameId{0};
  return sample;
}
void Counterexamples() {
  constexpr auto contract = SourceContract::kLanguageSourceV1;
  auto original = Make(2);
  const auto expect = [&](const Sample& sample, ArrayInitializationError error) {
    const auto result = BuildArrayInitializations(sample.graph, contract, sample.Constants(), sample.names);
    Check(std::holds_alternative<ArrayInitializationError>(result));
    if (std::get<ArrayInitializationError>(result) != error) std::cerr << "expected " << unsigned(error) << " actual " << unsigned(std::get<ArrayInitializationError>(result)) << "\n";
    Check(std::get<ArrayInitializationError>(result) == error);
  };
  auto direct = original;
  const auto cursor = *direct.graph.ops[1].result;
  Check(!ReplaceUse(direct.graph, {direct.graph.ops[3].first_use.value + 1}, cursor));
  Check(!ReplaceUse(direct.graph, direct.graph.ops[4].first_use, cursor));
  Check(std::holds_alternative<ArrayInitializationPlan>(BuildArrayInitializations(direct.graph, contract, direct.Constants(), direct.names)));
  auto changed = original;
  Check(!ReplaceUse(changed.graph, {changed.graph.ops[1].first_use.value}, ValueId{0}));
  expect(changed, ArrayInitializationError::kDestination);
  changed = original;
  Check(!ReplaceLiteralUse(changed.graph, {changed.graph.ops[5].first_use.value + 1}, abcd::analysis::IntegerLiteral{0}));
  expect(changed, ArrayInitializationError::kCursor);
  // A source operand cannot also expose the private destination to an iterator.
  changed = original;
  Check(!ReplaceUse(changed.graph, {changed.graph.ops[1].first_use.value + 2}, *changed.graph.ops[0].result));
  expect(changed, ArrayInitializationError::kOperand);
  changed = original; changed.constants[2] = std::monostate{};
  expect(changed, ArrayInitializationError::kOrder);
  auto built = std::get<ArrayInitializationPlan>(BuildArrayInitializations(original.graph, contract, original.Constants(), original.names));
  const auto reject = [&](const ArrayInitializationPlan& plan) {
    Check(std::holds_alternative<ArrayInitializationError>(VerifyArrayInitializations(original.graph, plan, contract, original.Constants(), original.names)));
  };
  auto forged = built; forged.steps[0].value = UseId{original.graph.ops[1].first_use.value}; reject(forged);
  forged = built; std::swap(forged.steps[0], forged.steps[1]); reject(forged);
  forged = built; forged.steps[1] = forged.steps[0]; reject(forged);
  forged = built; forged.arrays[0].allocation = {1}; reject(forged);
  forged = built; forged.metadata.length_reads[2] = 0; reject(forged);
  forged = built; forged.contract = SourceContract::kLanguageSourceV5; reject(forged);
}
void Trees() {
  for (unsigned count : {1U, 200U, 201U, 400U, 800U}) for (bool split : {false, true}) for (bool late : {false, true}) {
    auto sample = Make(count, true, split, false, late);
    sample.names.push_back(u"input");
    constexpr auto contract = SourceContract::kLanguageSourceV1;
    std::optional<ArrayInitializationError> cause;
    const auto planned = BuildEvaluationPlan(sample.graph, {}, IncomingCells::kReject, {}, {}, {}, {},
        ClosureWrites::kUnknown, {}, {}, {}, nullptr, {}, {contract, sample.Constants(), sample.names, &cause});
    if (const auto* error = std::get_if<EvaluationFailure>(&planned)) std::cerr << "evaluation " << unsigned(error->kind) << " count=" << count << " split=" << split << " late=" << late << " array=" << (cause ? unsigned(*cause) : 999) << "\n";
    Check(std::holds_alternative<EvaluationPlan>(planned));
    const auto& evaluation = std::get<EvaluationPlan>(planned);
    const auto control = std::get<ControlPlan>(BuildControlPlan(sample.graph));
    FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction;
    syntax.strictness = Strictness::kStrict; syntax.identity.contract = contract;
    syntax.names = sample.names; syntax.parameters = {{NameId{1}, NameEvidence::kDebugRange}};
    const auto built = BuildFunctionTree(sample.graph, control, evaluation, syntax, sample.Constants());
    if (const auto* error = std::get_if<FunctionTreeFailure>(&built)) std::cerr << "tree " << unsigned(error->kind) << "\n";
    Check(std::holds_alternative<ConstructedFunction>(built));
    const auto& tree = std::get<ConstructedFunction>(built).tree;
    Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(sample.graph, control, evaluation, tree, sample.Constants())));
    std::ostringstream text; Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, text)));
    Check(text.str().find("...") != std::string::npos);
    Check(text.str().find("input") != std::string::npos);

    const auto rejected = [&](const FunctionTree& changed) {
      Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(sample.graph, control, evaluation, changed, sample.Constants())));
    };
    auto changed = tree; bool changed_spread = false;
    for (auto& node : changed.expressions) if (auto* element = std::get_if<ArrayElement>(&node.expression))
      if (const auto* spread = std::get_if<ArraySpreadElement>(&element->element)) {
        element->element = ArrayValueElement{spread->iterable}; changed_spread = true; break;
      }
    Check(changed_spread); rejected(changed);
    changed = tree;
    for (auto& node : changed.expressions) if (auto* array = std::get_if<ArrayExpr>(&node.expression)) {
      std::swap(changed.arguments[array->elements.first], changed.arguments[array->elements.first + 1]); break;
    }
    rejected(changed);
    changed = tree;
    std::optional<ExprId> literal;
    for (const auto& node : tree.expressions) if (const auto* element = std::get_if<ArrayElement>(&node.expression))
      if (const auto* value = std::get_if<ArrayValueElement>(&element->element)) { literal = value->value; break; }
    Check(literal.has_value());
    for (auto& node : changed.expressions) if (auto* element = std::get_if<ArrayElement>(&node.expression))
      if (auto* spread = std::get_if<ArraySpreadElement>(&element->element)) { spread->iterable = *literal; break; }
    rejected(changed);
    changed = tree; changed.identity.contract = SourceContract::kLanguageSourceV5; rejected(changed);
  }
}
void LateCounterexamples() {
  constexpr auto contract = SourceContract::kLanguageSourceV1;
  auto original = Make(2, true, true, false, true);
  const auto build = [&](const Sample& s) { return BuildArrayInitializations(s.graph, contract, s.Constants(), s.names); };
  const auto proof = std::get<ArrayInitializationPlan>(build(original));
  std::vector<UseId> rhs;
  for (const auto& step : proof.steps) if (step.value) rhs.push_back(*step.value);
  Check(rhs.size() == 4);
  const auto rejected = [&](const Sample& s, ArrayInitializationError error) {
    auto result = build(s); Check(std::holds_alternative<ArrayInitializationError>(result));
    if (std::get<ArrayInitializationError>(result) != error) std::cerr << "expected " << unsigned(error) << " actual " << unsigned(std::get<ArrayInitializationError>(result)) << "\n";
    Check(std::get<ArrayInitializationError>(result) == error);
  };
  auto changed = original;
  const auto first = std::get<ValueId>(changed.graph.uses[rhs[0].value].operand);
  const auto second = std::get<ValueId>(changed.graph.uses[rhs[1].value].operand);
  Check(!ReplaceUse(changed.graph, rhs[0], second)); Check(!ReplaceUse(changed.graph, rhs[1], first));
  rejected(changed, ArrayInitializationError::kCorrespondence); // future SSA definition
  changed = original; Check(!ReplaceUse(changed.graph, rhs[1], first));
  rejected(changed, ArrayInitializationError::kSharedOperand);
  changed = original; Check(!ReplaceUse(changed.graph, rhs[0], ValueId{0}));
  rejected(changed, ArrayInitializationError::kOrder); // unowned call is still observable
  auto forged = proof;
  auto nested = std::find_if(forged.nested.begin(), forged.nested.end(), [](const auto& use) { return use.has_value(); });
  Check(nested != forged.nested.end()); nested->reset();
  Check(std::get<ArrayInitializationError>(VerifyArrayInitializations(original.graph, forged, contract,
      original.Constants(), original.names)) == ArrayInitializationError::kCorrespondence);
  // Valid SSA with reversed sibling call results: availability alone is not order.
  Fixture f; f.graph.values.push_back({EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}});
  f.Add(ValueArrayAllocation{}, true, {}); const auto array = *f.graph.ops.back().result;
  f.graph.ops.back().normal = LanguageEffect::kAllocation;
  const auto call = [&](std::initializer_list<std::pair<OperandRole, ValueOperand>> args, unsigned count) {
    f.Add(ValueInvoke{InvokeReceiver::kUndefined, count}, true, args);
    f.graph.ops.back().normal = f.graph.ops.back().before_throw = LanguageEffect::kUserCode;
    f.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
    return *f.graph.ops.back().result;
  };
  const auto left = call({{OperandRole::kCallee, ValueId{0}}}, 0);
  const auto right = call({{OperandRole::kCallee, ValueId{0}}}, 0);
  const auto source = call({{OperandRole::kCallee, ValueId{0}}, {OperandRole::kArgument, right}, {OperandRole::kArgument, left}}, 2);
  f.Add(ValueArraySpread{}, true, {{OperandRole::kDestination, array}, {OperandRole::kIndex, abcd::analysis::IntegerLiteral{0}}, {OperandRole::kSource, source}});
  f.graph.ops.back().normal = f.graph.ops.back().before_throw = LanguageEffect::kUserCode | LanguageEffect::kHeapWrite;
  f.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
  f.Add(ValueReturn{}, false, {{OperandRole::kReturnValue, array}});
  f.graph.blocks = {{{0}, static_cast<std::uint32_t>(f.graph.ops.size()), {0}, 0}};
  Sample siblings{std::move(f.graph)}; siblings.constants.resize(siblings.graph.ops.size());
  rejected(siblings, ArrayInitializationError::kOrder);
  const auto& parent = siblings.graph.ops[3];
  Check(!ReplaceUse(siblings.graph, {parent.first_use.value + 1}, left));
  Check(!ReplaceUse(siblings.graph, {parent.first_use.value + 2}, right));
  Check(std::holds_alternative<ArrayInitializationPlan>(build(siblings)));
  // Exact byte admission includes the entire ownership/worklist scratch.
  std::size_t lo = 0, hi = 1000000;
  while (lo < hi) {
    const auto mid = lo + (hi - lo) / 2;
    const auto result = BuildArrayInitializations(original.graph, contract, original.Constants(), original.names, {mid});
    if (std::holds_alternative<ArrayInitializationPlan>(result)) hi = mid;
    else { Check(std::get<ArrayInitializationError>(result) == ArrayInitializationError::kResourceLimit); lo = mid + 1; }
  }
  Check(std::holds_alternative<ArrayInitializationPlan>(BuildArrayInitializations(original.graph, contract, original.Constants(), original.names, {lo})));
  Check(std::get<ArrayInitializationError>(BuildArrayInitializations(original.graph, contract, original.Constants(), original.names, {lo - 1})) == ArrayInitializationError::kResourceLimit);
}
int main() {
  LateCounterexamples();
  Trees();
  Counterexamples();
  constexpr auto contract=SourceContract::kLanguageSourceV1;
  for(unsigned n:{1U,200U,201U,400U,800U}) for(bool split:{false,true}) {
    auto sample=Make(n,true,split);
    auto built=BuildArrayInitializations(sample.graph,contract,sample.Constants(),sample.names);
    if(auto* error=std::get_if<ArrayInitializationError>(&built)) std::cerr<<"error "<<unsigned(*error)<<" n="<<n<<" split="<<split<<"\n";
    Check(std::holds_alternative<ArrayInitializationPlan>(built));
    auto plan=std::get<ArrayInitializationPlan>(built);
    Check(plan.arrays.size()==1 && plan.steps.size()==4*n);
    Check(std::holds_alternative<std::uint64_t>(VerifyArrayInitializations(sample.graph,plan,contract,sample.Constants(),sample.names)));
    auto changed=plan;changed.steps[0].after.offset++;
    Check(std::get<ArrayInitializationError>(VerifyArrayInitializations(sample.graph,changed,contract,sample.Constants(),sample.names))==ArrayInitializationError::kCorrespondence);
    changed=plan;changed.steps.pop_back();
    Check(std::holds_alternative<ArrayInitializationError>(VerifyArrayInitializations(sample.graph,changed,contract,sample.Constants(),sample.names)));
    changed=plan;changed.owners[plan.steps[0].operation.value].reset();
    Check(std::holds_alternative<ArrayInitializationError>(VerifyArrayInitializations(sample.graph,changed,contract,sample.Constants(),sample.names)));
    if(n==1 && !split) {
      std::size_t lo=0,hi=1000000;
      while(lo<hi) { auto mid=lo+(hi-lo)/2;auto result=BuildArrayInitializations(sample.graph,contract,sample.Constants(),sample.names,{mid});
        if(std::holds_alternative<ArrayInitializationPlan>(result))hi=mid;else {Check(std::get<ArrayInitializationError>(result)==ArrayInitializationError::kResourceLimit);lo=mid+1;} }
      Check(lo>0 && std::holds_alternative<ArrayInitializationPlan>(BuildArrayInitializations(sample.graph,contract,sample.Constants(),sample.names,{lo})));
      Check(std::get<ArrayInitializationError>(BuildArrayInitializations(sample.graph,contract,sample.Constants(),sample.names,{lo-1}))==ArrayInitializationError::kResourceLimit);
    }
  }
  auto bad=Make(1,false,false,true);
  Check(std::get<ArrayInitializationError>(BuildArrayInitializations(bad.graph,contract,bad.Constants(),bad.names))==ArrayInitializationError::kCursor);
  Check(std::get<ArrayInitializationError>(BuildArrayInitializations(bad.graph,static_cast<SourceContract>(0),bad.Constants(),bad.names))==ArrayInitializationError::kContract);
}
