#include "../../../src/source/exception_tree.h"
#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/function_values.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_structure.h"
#include "../../../src/source/source_variables.h"
#include "../../support/analysis/ssa_fixture.h"
#include <iostream>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
using test::Check;
namespace {
template<class T, class V> T Take(V v, std::source_location at = std::source_location::current()) {
  if (!std::holds_alternative<T>(v)) {
    std::cerr << at.line() << " variant " << v.index() << '\n';
    std::visit([](const auto& x) { if constexpr (requires { x.kind; }) std::cerr << unsigned(x.kind) << '\n'; }, v);
    std::abort();
  }
  return std::get<T>(std::move(v));
}
abc::MethodBody Body(unsigned count, bool copies, bool split, bool reverse, unsigned saved, bool statement) {
  std::vector<std::uint8_t> bytes;
  const auto emit = [&](abc::Opcode op) { bytes.push_back(static_cast<std::uint8_t>(op)); };
  const auto load = [&](unsigned r) { emit(abc::Opcode::kLDA_V8); bytes.push_back(r); };
  const auto read = [&](unsigned name) { load(3); emit(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.insert(bytes.end(), {0, static_cast<std::uint8_t>(name), 0}); };
  const auto jump = [&](abc::Opcode op) { auto pc = bytes.size(); emit(op); bytes.push_back(0); return pc; };
  const auto patch = [&](std::size_t pc) { Check(bytes.size() - pc < 128); bytes[pc + 1] = bytes.size() - pc; };
  for (unsigned i = 0; i < count; ++i) {
    if (count > 1) {
      // Scale the retained boolean-selection prefix itself, rather than the
      // unrelated call-reference machinery of the effect-rich unit fixture.
      load(3); emit(abc::Opcode::kISTRUE);
      const auto shared = jump(abc::Opcode::kJNEZ_IMM8);
      read(1); emit(abc::Opcode::kISTRUE);
      const auto no = jump(abc::Opcode::kJEQZ_IMM8);
      emit(abc::Opcode::kLDTRUE); const auto join = jump(abc::Opcode::kJMP_IMM8);
      patch(no); emit(abc::Opcode::kLDFALSE); patch(join);
      emit(abc::Opcode::kISFALSE); const auto yes = jump(abc::Opcode::kJEQZ_IMM8);
      const auto end = jump(abc::Opcode::kJMP_IMM8);
      patch(shared); patch(yes); read(5); patch(end);
      continue;
    }
    read(0); emit(abc::Opcode::kISTRUE);
    const auto shared = jump(reverse ? abc::Opcode::kJEQZ_IMM8 : abc::Opcode::kJNEZ_IMM8);
    if (split) { emit(abc::Opcode::kJMP_IMM8); bytes.push_back(2); }
    if (statement) emit(abc::Opcode::kDEBUGGER);
    read(1); emit(abc::Opcode::kISTRUE);
    const auto alternate = jump(abc::Opcode::kJEQZ_IMM8);
    read(2); const auto merge = jump(abc::Opcode::kJMP_IMM8);
    patch(alternate); read(3); patch(merge);
    emit(abc::Opcode::kSTA_V8); bytes.push_back(saved);
    if (copies) { emit(abc::Opcode::kMOV_V8_V8); bytes.insert(bytes.end(), {2, static_cast<std::uint8_t>(saved)}); load(2); emit(abc::Opcode::kSTA_V8); bytes.push_back(saved); }
    read(4); emit(abc::Opcode::kSTRICTEQ_IMM8_V8); bytes.insert(bytes.end(), {0, static_cast<std::uint8_t>(saved)});
    const auto yes = jump(abc::Opcode::kJNEZ_IMM8);
    read(6); emit(abc::Opcode::kCALLTHIS0_IMM8_V8); bytes.insert(bytes.end(), {0, 3});
    const auto end = jump(abc::Opcode::kJMP_IMM8);
    patch(shared); patch(yes); read(5); emit(abc::Opcode::kCALLTHIS0_IMM8_V8); bytes.insert(bytes.end(), {0, 3});
    patch(end); read(7);
  }
  emit(abc::Opcode::kRETURN);
  const auto handler_pc = static_cast<std::uint32_t>(bytes.size()); emit(abc::Opcode::kRETURN);
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset={0}; region.length=handler_pc; region.handler_count=1;
  abc::CatchHandler handler; handler.offset={handler_pc}; handler.length=1;
  body.tries.push_back(region); body.handlers.push_back(handler); return body;
}
std::pair<std::uint64_t, std::size_t> Run(unsigned count, bool copies = false, bool split = false, bool reverse = false, unsigned saved = 0, bool statement = false) {
  const auto body = Body(count, copies, split, reverse, saved, statement);
  const auto frame = Take<abc::FrameLayout>(abc::BuildFrameLayout(3, 1, {}));
  RouteInputIdentity identity; identity.profile = abc::ProfileId::kDynamic1206;
  const auto flow=Take<ExceptionFlow>(BuildExceptionFlow(body,identity.profile,frame.registers));
  IdfQuota quota{kDefaultSemanticWork};const auto ssa=Take<ExceptionSsa>(ExceptionSsa::Build(flow,frame.registers,quota));
  const auto environments=Take<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  std::vector<EntryValueFact> entries;
  for(std::size_t i=0;i<ssa.Values().size();++i) {
    const auto& value=ssa.Values()[i];const auto* reg=std::get_if<abc::RegisterId>(&value.slot);
    if(std::holds_alternative<MethodEntry>(value.origin)&&reg&&reg->value>=frame.locals)
      entries.push_back({{static_cast<std::uint32_t>(i)},ValueKinds::Language()});
  }
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values=Take<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,entries,value_work,{},
      PendingExceptionValues::kLanguage,{}, {},CallResultValues::kLanguage));
  const auto proof=Take<ExceptionInputProof>(VerifyExceptionInputs(flow,ssa,environments,values));
  const PhasedValueInput input{flow,ssa,environments,values,proof,frame};
  const auto graph=Take<ValueProgram>(ImportPhasedValues(input));
  const auto evaluation=Take<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));
  const auto regions=Take<ExceptionPlan>(BuildExceptionPlan(body,graph,input));
  const auto control=Take<ExceptionControl>(BuildExceptionControl(body,graph,input,regions));
  const ExceptionTreeInput exceptions{body,input,regions,control};
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = {100}; syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"o", u"first", u"flag", u"left", u"right", u"test", u"yes", u"no", u"later"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) if (const auto* r = std::get_if<ValueNamedRead>(&graph.ops[i].operation)) constants[i] = NameId{r->name.value + 1};
  const ExpressionConstants resolved{constants, syntax.names.size(), 0};
  const auto build = [&](SemanticBudget budget) { return BuildExceptionFunctionDraft(graph, exceptions, evaluation, syntax, resolved, budget); };
  const SemanticBudget allowance{128 * 1024 * 1024}; // Existing record cap, not a production limit change.
  const auto built = Take<ExceptionFunctionDraft>(build(allowance));
  Take<TreeSemantics>(VerifyExceptionTreeSemantics(graph, exceptions, evaluation, built.tree, resolved, allowance));
  unsigned retained_prefixes = 0;
  for (const auto& placed : built.tree.operations)
    retained_prefixes += std::holds_alternative<ValueBranch>(graph.ops[placed.operation.value].operation) &&
        evaluation.operations[placed.operation.value].kind == EvaluationKind::kRoot && std::holds_alternative<ExprId>(placed.node) &&
        std::holds_alternative<AssignmentExpr>(built.tree.expressions[std::get<ExprId>(placed.node).value].expression);
  Check(retained_prefixes == count);
  if (count == 1) {
    Check(built.flow && built.flow->original);
    const auto& original = *built.flow->original;
    // Recovery accepts an already checked immutable tree; structural/edge
    // mutations must fail TreeFlow before reaching that API, below.
    Take<SourceVariables>(PrepareSourceVariables(built.tree, {}, &original));
    auto stale = original; ++stale.cfg_revision.value;
    Check(std::get<TreeStructureError>(PrepareSourceVariables(built.tree, {}, &stale)) == TreeStructureError::kOwnership);
    unsigned changed = 0;
    for (std::size_t i = 0; i < built.tree.expressions.size(); ++i) if (std::holds_alternative<ConditionalExpr>(built.tree.expressions[i].expression)) {
      auto bad = built.tree; auto& choice = std::get<ConditionalExpr>(bad.expressions[i].expression);
      std::swap(choice.consequent, choice.alternate);
      Check(!std::holds_alternative<TreeStructureError>(VerifyTreeStructure(bad)));
      Check(!std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(graph, exceptions, evaluation, bad, resolved)));
      ++changed;
    }
    Check(changed != 0);
    // Changing an original exception edge must not be hidden by the selected
    // prefix's comma representation or by the builder's preverified control.
    auto wrong = graph;
    const auto thrown = std::find_if(wrong.edges.begin(),wrong.edges.end(),[](const auto& e){return e.kind==FlowEdgeKind::kException;});
    Check(thrown!=wrong.edges.end()); thrown->target=wrong.ops.front().block;
    Check(!std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(wrong,exceptions,evaluation,built.tree,resolved)));
    // Moving the selection relative to surrounding roots changes the event
    // order even if its operands and its phi destination are unchanged.
    unsigned moved=0;
    for (std::size_t i=0;i<built.tree.expressions.size();++i) {
      const auto* sequence=std::get_if<SequenceExpr>(&built.tree.expressions[i].expression);
      if(!sequence)continue;
      auto bad=built.tree;
      auto& swapped=std::get<SequenceExpr>(bad.expressions[i].expression);
      std::swap(swapped.left,swapped.right);
      Check(!std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(graph,exceptions,evaluation,bad,resolved)));
      ++moved;
    }
    Check(moved!=0);
    // Missing/duplicated original event placements must not be excused by the
    // selected region's prefix role (the independent coverage check owns this).
    const auto retained = std::find_if(built.tree.operations.begin(), built.tree.operations.end(), [&](const auto& p) {
      return std::holds_alternative<ValueBranch>(graph.ops[p.operation.value].operation) &&
          evaluation.operations[p.operation.value].kind == EvaluationKind::kRoot && std::holds_alternative<ExprId>(p.node) &&
          std::holds_alternative<AssignmentExpr>(built.tree.expressions[std::get<ExprId>(p.node).value].expression);
    });
    Check(retained != built.tree.operations.end());
    auto missing = built.tree; missing.operations.erase(missing.operations.begin() + (retained - built.tree.operations.begin()));
    Check(!std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(graph, exceptions, evaluation, missing, resolved)));
    auto duplicate = built.tree; duplicate.operations.push_back(*retained);
    Check(!std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(graph, exceptions, evaluation, duplicate, resolved)));
  }
  std::size_t low = 0, high = allowance.bytes;
  while (low + 1 < high) { const auto mid = low + (high - low) / 2; if (std::holds_alternative<ExceptionFunctionDraft>(build({mid, SemanticBudget{}.work}))) high = mid; else low = mid; }
  Take<ExceptionFunctionDraft>(build({high, SemanticBudget{}.work}));
  Check(std::get<FunctionTreeFailure>(build({high - 1, SemanticBudget{}.work})).kind == FunctionTreeError::kResourceLimit);
  Take<ExceptionFunctionDraft>(build({allowance.bytes, built.work}));
  Check(std::get<FunctionTreeFailure>(build({allowance.bytes, built.work - 1})).kind == FunctionTreeError::kWorkLimit);
  return {built.work, high};
}
}
int main() {
  for (bool copies : {false, true}) for (bool split : {false, true}) for (bool reverse : {false, true}) for (unsigned saved : {0U, 1U}) Run(1, copies, split, reverse, saved);
  std::pair<std::uint64_t, std::size_t> previous{};
  for (unsigned n : {200, 201, 400, 800}) {
    const auto cost = Run(n); if (previous.first) Check(cost.first < 3 * previous.first && cost.second < 3 * previous.second);
    previous = cost; std::cout << n << " work=" << cost.first << " bytes=" << cost.second << '\n';
  }
}
