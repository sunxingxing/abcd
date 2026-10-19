#include "../../../src/source/function_tree.h"
#include "../../../src/source/call_plan.h"
#include "../../../src/source/tree_semantics.h"
#include "abcd/source/tree_printer.h"
#include <sstream>
#include "abcd/analysis/value_simplify.h"
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
  f.Add(ValueInvoke{InvokeReceiver::kExplicit,1,CallArgumentForm::kApplyArray},true,
      {{OperandRole::kCallee,ValueId{0}}, {OperandRole::kReceiver,SpecialValue::kUndefined},
       {OperandRole::kArgument,expose?cursor:ValueOperand{array}}});
  f.graph.ops.back().normal=f.graph.ops.back().before_throw=LanguageEffect::kUserCode|LanguageEffect::kHeapRead|LanguageEffect::kHeapWrite|LanguageEffect::kAllocation;
  f.graph.ops.back().exceptional=ExceptionalCompletion::kMayThrow;
  f.Add(ValueReturn{},false,{{OperandRole::kReturnValue,*f.graph.ops.back().result}});
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

int main() {
  for (unsigned raw=0; raw<=static_cast<unsigned>(SourceContract::kLanguageSourceV5); ++raw) {
    const auto c=static_cast<SourceContract>(raw);
    Check(CallSpread(c)==(c==SourceContract::kLanguageSourceV5 ? CallSpreadSemantics::kPrivateArgumentsV1 : CallSpreadSemantics::kPinnedRuntime));
  }
  for (unsigned count : {1U,200U,201U,400U,800U}) for (bool split : {false,true}) {
    auto sample=Make(count,true,split); sample.names.push_back(u"input");
    if(split) {
      const auto simplified=SimplifyValues(sample.graph,sample.graph.revision);
      Check(std::holds_alternative<ValueSimplifyStats>(simplified));
    }
    constexpr auto contract=SourceContract::kLanguageSourceV5;
    const auto build=[&](SourceContract c, SemanticBudget budget=SemanticBudget{}) {
      return BuildEvaluationPlan(sample.graph,budget,IncomingCells::kReject,{},{},{},{},ClosureWrites::kUnknown,
          {},{},{},nullptr,{}, {c,sample.Constants(),sample.names});
    };
    const auto old=build(SourceContract::kLanguageSourceV4);
    Check(std::holds_alternative<EvaluationFailure>(old));
    const auto& failure=std::get<EvaluationFailure>(old);
    Check(std::get<CallSpreadError>(failure.evaluation.plan_cause)==CallSpreadError::kPremise);
    const auto planned=build(contract);
    if(const auto* error=std::get_if<EvaluationFailure>(&planned)) std::cerr<<"eval "<<unsigned(error->kind)<<"\n";
    Check(std::holds_alternative<EvaluationPlan>(planned));
    const auto& evaluation=std::get<EvaluationPlan>(planned);
    // Cursor elision is licensed only by the independently verified private
    // construction. Neither a missing cursor classification nor a forged one
    // on an observable invocation may pass the evaluation verifier.
    auto forged=evaluation;
    for (auto& placement : forged.operations) if (placement.kind==EvaluationKind::kArrayCursor) {
      placement.kind=EvaluationKind::kRoot;break;
    }
    Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(sample.graph,forged)));
    forged=evaluation;
    forged.operations[sample.graph.ops.size()-2]={EvaluationKind::kArrayCursor,{},{}};
    Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(sample.graph,forged)));
    auto calls=BuildCallPlan(sample.graph,evaluation);
    if(split) {
      // The existing normal-flow scheduler retains effectful cross-block
      // roots. Construction/copy identity is verified, but publication keeps
      // the typed order refusal rather than overriding that scheduler.
      Check(std::holds_alternative<CallPlanFailure>(calls));
      Check(std::get<CallSpreadError>(std::get<CallPlanFailure>(calls).cause)==CallSpreadError::kOrder);
      continue;
    }
    if(const auto* error=std::get_if<CallPlanFailure>(&calls)) std::cerr<<"call "<<unsigned(error->kind)<<" cause="<<(std::holds_alternative<CallSpreadError>(error->cause)?unsigned(std::get<CallSpreadError>(error->cause)):999)<<"\n";
    Check(std::holds_alternative<CallPlan>(calls));
    auto plan=std::get<CallPlan>(calls);
    const auto call=sample.graph.ops.size()-2;
    Check(std::get<ValueCall>(plan.operations[call]).array.has_value());
    Check(std::holds_alternative<std::uint64_t>(VerifyCallPlan(sample.graph,evaluation,plan)));
    std::get<ValueCall>(plan.operations[call]).array.reset();
    Check(std::holds_alternative<CallPlanFailure>(VerifyCallPlan(sample.graph,evaluation,plan)));
    const auto control=std::get<ControlPlan>(BuildControlPlan(sample.graph));
    FunctionSyntax syntax{};syntax.callable=CallableKind::kFunction;syntax.strictness=Strictness::kStrict;
    syntax.identity.contract=contract;syntax.names=sample.names;
    syntax.parameters={{NameId{1},NameEvidence::kDebugRange}};
    const auto tree=BuildFunctionTree(sample.graph,control,evaluation,syntax,sample.Constants());
    if(const auto* error=std::get_if<FunctionTreeFailure>(&tree)) std::cerr<<"tree "<<unsigned(error->kind)<<"\n";
    Check(std::holds_alternative<ConstructedFunction>(tree));
    std::ostringstream output;
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(std::get<ConstructedFunction>(tree).tree,output)));
    Check(output.str().find("...input")!=std::string::npos);
    Check(output.str().find("[...")==std::string::npos);
    auto changed=std::get<ConstructedFunction>(tree).tree;
    for(auto& node:changed.expressions) if(auto* call=std::get_if<CallExpr>(&node.expression);call && call->array_arguments) {
      call->array_arguments.reset(); break;
    }
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(sample.graph,control,evaluation,changed,sample.Constants())));
    changed=std::get<ConstructedFunction>(tree).tree;
    for(auto& node:changed.expressions) if(auto* array=std::get_if<ArrayExpr>(&node.expression)) {
      std::swap(changed.arguments[array->elements.first],changed.arguments[array->elements.first+1]);break;
    }
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(sample.graph,control,evaluation,changed,sample.Constants())));
    if (count == 1) {
      // Recompute from the original input: the tree cannot retain a stale
      // callee, receiver, cursor or exception identity after a graph edit.
      const auto rejects = [&](ValueProgram graph) {
        Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(
            graph, control, evaluation, std::get<ConstructedFunction>(tree).tree,
            sample.Constants())));
      };
      auto graph = sample.graph;
      graph.uses[graph.ops[call].first_use.value].operand = SpecialValue::kUndefined;
      rejects(std::move(graph));
      graph = sample.graph;
      graph.uses[graph.ops[call].first_use.value + 1].operand = abcd::analysis::IntegerLiteral{9};
      rejects(std::move(graph));
      graph = sample.graph;
      for (auto& op : graph.ops) if (std::holds_alternative<ValueArraySpread>(op.operation)) {
        graph.uses[op.first_use.value + 1].operand = abcd::analysis::IntegerLiteral{3}; break;
      }
      rejects(std::move(graph));
    }
    SemanticBudget exact;exact.work=std::get<CallPlan>(calls).work;
    Check(std::holds_alternative<CallPlan>(BuildCallPlan(sample.graph,evaluation,exact)));
    --exact.work;
    Check(std::get<CallPlanFailure>(BuildCallPlan(sample.graph,evaluation,exact))==CallPlanError::kWorkLimit);
    Check(std::holds_alternative<EvaluationFailure>(build(contract,{0,0})));
  }
}
