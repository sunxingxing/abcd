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
abc::MethodBody Body(unsigned count, bool copies, bool split, bool reverse, unsigned saved, bool statement, bool retained) {
  std::vector<std::uint8_t> bytes;
  const auto emit = [&](abc::Opcode op) { bytes.push_back(static_cast<std::uint8_t>(op)); };
  const auto load = [&](unsigned r) { emit(abc::Opcode::kLDA_V8); bytes.push_back(r); };
  const auto read = [&](unsigned name) { load(3); emit(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.insert(bytes.end(), {0, static_cast<std::uint8_t>(name), 0}); };
  const auto jump = [&](abc::Opcode op) { auto pc = bytes.size(); emit(op); bytes.push_back(0); return pc; };
  const auto patch = [&](std::size_t pc) { Check(bytes.size() - pc < 128); bytes[pc + 1] = bytes.size() - pc; };
  for (unsigned i = 0; i < count; ++i) {
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
    read(6);
    if (retained) { emit(abc::Opcode::kCALLTHIS1_IMM8_V8_V8); bytes.insert(bytes.end(), {0, 3, static_cast<std::uint8_t>(saved)}); }
    else { emit(abc::Opcode::kCALLTHIS0_IMM8_V8); bytes.insert(bytes.end(), {0, 3}); }
    const auto end = jump(abc::Opcode::kJMP_IMM8);
    patch(shared); patch(yes); read(5); emit(abc::Opcode::kCALLTHIS0_IMM8_V8); bytes.insert(bytes.end(), {0, 3});
    patch(end); read(7);
  }
  emit(abc::Opcode::kRETURN); return test::Body(bytes);
}
std::pair<std::uint64_t, std::size_t> Run(unsigned count, bool copies = false, bool split = false, bool reverse = false, unsigned saved = 0, bool statement = false) {
#ifdef ABCD_TEST_RETAINED_DECISION_PREFIX
  constexpr bool retained = true;
#else
  constexpr bool retained = false;
#endif
  const auto body = Body(count, copies, split, reverse, saved, statement, retained);
  const auto frame = Take<abc::FrameLayout>(abc::BuildFrameLayout(3, 1, {}));
  RouteInputIdentity identity; identity.profile = abc::ProfileId::kDynamic1206;
  const auto input = Take<FunctionInput>(FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed));
  const auto values = Take<FunctionValues>(BuildFunctionValues(input, body, frame)); const auto& graph = values.graph;
  const auto control = Take<ControlPlan>(BuildControlPlan(graph)); const auto evaluation = Take<EvaluationPlan>(BuildEvaluationPlan(graph));
  unsigned prefixes = 0;
  for (const auto& n : control.nodes) if (n.decision && n.decision->prefix == DecisionPrefix::kRegion) ++prefixes;
  Check(prefixes == count);
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = input.Method(); syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"o", u"first", u"flag", u"left", u"right", u"test", u"yes", u"no", u"later"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) if (const auto* r = std::get_if<ValueNamedRead>(&graph.ops[i].operation)) constants[i] = NameId{r->name.value + 1};
  const ExpressionConstants resolved{constants, syntax.names.size(), 0};
  const auto build = [&](SemanticBudget budget) { return BuildFunctionTree(graph, control, evaluation, syntax, resolved, budget); };
  const auto built = Take<ConstructedFunction>(build({}));
  Take<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, built.tree, resolved));
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
      Check(!std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, bad, resolved)));
      ++changed;
    }
    Check(changed != 0);
    for (std::size_t i = 0; i < control.nodes.size(); ++i) if (control.nodes[i].decision) {
      auto bad = control; const auto at = bad.nodes[i].decision->evaluate;
      bad.nodes[at.value].next = bad.nodes[i].decision->join;
      Check(!std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, bad, evaluation, built.tree, resolved)));
      break;
    }
    for (std::size_t i = 0; i < evaluation.logical_values.size(); ++i) if (evaluation.logical_values[i]) {
      auto bad = evaluation; bad.logical_values[i].reset();
      Check(!std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, bad, built.tree, resolved)));
      break;
    }
    // Missing/duplicated original event placements must not be excused by the
    // selected region's prefix role (the independent coverage check owns this).
    auto missing = built.tree; missing.operations.pop_back();
    Check(!std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, missing, resolved)));
    auto duplicate = built.tree; duplicate.operations.push_back(duplicate.operations.back());
    Check(!std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, duplicate, resolved)));
  }
  std::size_t low = 0, high = SemanticBudget{}.bytes;
  while (low + 1 < high) { const auto mid = low + (high - low) / 2; if (std::holds_alternative<ConstructedFunction>(build({mid, SemanticBudget{}.work}))) high = mid; else low = mid; }
  Take<ConstructedFunction>(build({high, SemanticBudget{}.work}));
  Check(std::get<FunctionTreeFailure>(build({high - 1, SemanticBudget{}.work})).kind == FunctionTreeError::kResourceLimit);
  Take<ConstructedFunction>(build({SemanticBudget{}.bytes, built.work}));
  Check(std::get<FunctionTreeFailure>(build({SemanticBudget{}.bytes, built.work - 1})).kind == FunctionTreeError::kWorkLimit);
  return {built.work, high};
}
}
int main() {
  for (bool copies : {false, true}) for (bool split : {false, true}) for (bool reverse : {false, true}) for (unsigned saved : {0U, 1U}) for (bool statement : {false, true}) Run(1, copies, split, reverse, saved, statement);
  std::pair<std::uint64_t, std::size_t> previous{};
  for (unsigned n : {200, 201, 400, 800}) {
    const auto cost = Run(n); if (previous.first) Check(cost.first < 3 * previous.first && cost.second < 3 * previous.second);
    previous = cost; std::cout << n << " work=" << cost.first << " bytes=" << cost.second << '\n';
  }
}
