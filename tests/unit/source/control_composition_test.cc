#include "../../../src/source/exception_tree.h"
#include "../../../src/source/family_print.h"
#include "../../../src/source/tree_flow.h"
#include "../../../src/source/tree_semantics.h"
#include "analysis/ssa_fixture.h"
#include "abcd/analysis/exception_locals.h"

#include <sstream>
#include <source_location>
#include <string_view>
#include <type_traits>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
template <class T, class... Alternatives>
T Take(std::variant<Alternatives...> value,
    std::source_location where = std::source_location::current()) {
  if (!std::holds_alternative<T>(value)) {
    std::cerr << "composition pipeline failure at line " << where.line() << '\n';
    std::visit([](const auto& error) {
      if constexpr (std::is_enum_v<std::decay_t<decltype(error)>>)
        std::cerr << "error " << static_cast<unsigned>(error) << '\n';
    }, value);
  }
  test::Check(std::holds_alternative<T>(value));
  return std::get<T>(std::move(value));
}
void Emit(const abc::MethodBody& body, abc::ProfileId profile, std::uint32_t locals,
    std::uint32_t parameters, bool staging, std::ostream* output,
    std::optional<ScopeKind> cell_scope = {});

void LoopCarried(abc::ProfileId profile, std::ostream* output) {
  // x = initial; try { while (next()) { x = step(x); next(); } } catch (_) {}
  // return x. Three throwing sites must observe the current loop-carried x,
  // never a failed call result or the function's original entry value.
  const std::vector<std::uint8_t> bytes = {
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 1,
      static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 2,
      static_cast<std::uint8_t>(abc::Opcode::kCALLARG0_IMM8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kJEQZ_IMM8), 15,
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 3,
      static_cast<std::uint8_t>(abc::Opcode::kCALLARG1_IMM8_V8), 0, 0,
      static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 2,
      static_cast<std::uint8_t>(abc::Opcode::kCALLARG0_IMM8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-17),
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kRETURN),
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kRETURN)};
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {4}; region.length = 19; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {26}; handler.length = 3;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 1, 3, true, output);
}
void Emit(const abc::MethodBody& body, abc::ProfileId profile, std::uint32_t locals,
    std::uint32_t parameters, bool staging, std::ostream* output, std::optional<ScopeKind> cell_scope) {
  const auto frame = Take<abc::FrameLayout>(abc::BuildFrameLayout(locals, parameters, {}));
  const auto flow = Take<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers, {}, {}, &frame));
  IdfQuota quota{kDefaultSemanticWork};
  const auto ssa = Take<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
  std::vector<EntryValueFact> entries;
  for (std::size_t v = 0; v < ssa.Values().size(); ++v) {
    const auto& value = ssa.Values()[v];
    const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
    if (std::holds_alternative<MethodEntry>(value.origin) && reg && reg->value >= frame.locals)
      entries.push_back({{static_cast<std::uint32_t>(v)}, ValueKinds::Language()});
  }
  const auto environments = Take<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  const auto lexical = Take<ExceptionLocals>(ProveExceptionLocals(flow, environments));
  IdfQuota value_quota{kDefaultSemanticWork};
  const auto values = Take<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
      value_quota, {}, PendingExceptionValues::kLanguage, lexical.loads, {}, CallResultValues::kLanguage));
  const auto proof = Take<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, environments, values));
  const PhasedValueInput input{flow, ssa, environments, values, proof, frame};
  const auto graph = Take<ValueProgram>(ImportPhasedValues(input));
  const auto regions = Take<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
  const auto built_control = BuildExceptionControl(body, graph, input, regions);
  if (const auto* error = std::get_if<ExceptionControlError>(&built_control))
    std::cerr << "loop-carried control error " << static_cast<unsigned>(*error) << '\n';
  test::Check(std::holds_alternative<ExceptionControl>(built_control));
  const auto& control = std::get<ExceptionControl>(built_control);
  test::Check(control.skeleton.loops.size() == 1);
  const auto planned = BuildPhasedEvaluationPlan(graph);
  if (const auto* error = std::get_if<EvaluationError>(&planned))
    std::cerr << "composition evaluation error " << static_cast<unsigned>(*error) << '\n';
  test::Check(std::holds_alternative<EvaluationPlan>(planned));
  const auto& evaluation = std::get<EvaluationPlan>(planned);
  if (staging) test::Check(evaluation.exceptions && !evaluation.exceptions->slots.empty());
  else if (!evaluation.cells) {
    test::Check(evaluation.phis.has_value());
    bool empty_schedule = false;
    for (const auto& bundle : evaluation.phis->bundles)
      empty_schedule |= bundle.schedule.steps.empty();
    test::Check(empty_schedule); // Regression: no orphan origin for erased self-copies.
  }
  std::vector<ExpressionConstant> constants(graph.ops.size());
  FunctionSyntax syntax{};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.parameters.resize(frame.parameters);
  const ExceptionTreeInput context{body, input, regions, control};
  const auto built = BuildExceptionFunctionDraft(graph, context, evaluation, syntax, {constants, 0});
  if (const auto* error = std::get_if<FunctionTreeError>(&built))
    std::cerr << "loop-carried tree error " << static_cast<unsigned>(*error) << '\n';
  test::Check(std::holds_alternative<ExceptionFunctionDraft>(built));
  const auto& tree = std::get<ExceptionFunctionDraft>(built).tree;
  if (evaluation.cells) {
    std::size_t locals = 0;
    for (std::size_t i = 0; i < tree.symbols.size(); ++i) {
      const auto& symbol = tree.symbols[i];
      if (symbol.kind != BindingKind::kLocal) continue;
      ++locals;
      test::Check(symbol.scope != tree.body_scope);
      if (cell_scope) test::Check(tree.scopes[symbol.scope.value].kind == *cell_scope);
      auto hoisted = tree;
      hoisted.symbols[i].scope = tree.body_scope;
      test::Check(std::holds_alternative<TreeFlowError>(
          VerifyExceptionTreeFlow(graph, context, evaluation, hoisted)));
    }
    test::Check(locals == 1);
  }
  test::Check(tree.recovery_issues.empty());
  test::Check(std::holds_alternative<TreeFlow>(VerifyExceptionTreeFlow(graph, context, evaluation, tree)));
  test::Check(std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(
      graph, context, evaluation, tree, {constants, 0})));
  const auto family = Take<ConstructedFamily>(BuildFunctionFamily({{tree, {}, {}}}, tree.owner));
  std::ostringstream source;
  test::Check(std::holds_alternative<PrintedFamilyDraft>(PrintFamilyDraft(family, source)));
  if (output) *output << source.str() << ",\n";
}
void RetryCarried(abc::ProfileId profile, std::ostream* output) {
  // x = initial; while(next()) { try { x = step(x); }
  // catch(e) { observe(x, e); } } return x. The observer is outside protection.
  const std::vector<std::uint8_t> bytes = {
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 2,
      static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 3,
      static_cast<std::uint8_t>(abc::Opcode::kCALLARG0_IMM8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kJEQZ_IMM8), 21,
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 4,
      static_cast<std::uint8_t>(abc::Opcode::kCALLARG1_IMM8_V8), 0, 0,
      static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-13),
      static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 1,
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 5,
      static_cast<std::uint8_t>(abc::Opcode::kCALLARGS2_IMM8_V8_V8), 0, 0, 1,
      static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-23),
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kRETURN)};
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {10}; region.length = 7; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {19}; handler.length = 10;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 2, 4, false, output);
}
void IterationCell(abc::ProfileId profile, std::ostream* output) {
  // Every iteration allocates a fresh lexical cell. Normal and exceptional
  // writes join before the observer reads it; pop precedes the loop backedge.
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  const std::vector<std::uint8_t> bytes = {
      op(abc::Opcode::kLDA_V8), 1,
      op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 28,
      op(abc::Opcode::kNEWLEXENV_IMM8), 1,
      op(abc::Opcode::kLDA_V8), 2,
      op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
      op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
      op(abc::Opcode::kJMP_IMM8), 4,
      op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
      op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0,
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 3,
      op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 0,
      op(abc::Opcode::kPOPLEXENV),
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-30),
      op(abc::Opcode::kRETURNUNDEFINED)};
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {12}; region.length = 4; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {18}; handler.length = 2;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 1, 3, false, output, ScopeKind::kBlock);
}
void CatchCell(abc::ProfileId profile, std::ostream* output) {
  // A catch-owned environment is allocated only on failure, initialized with
  // this iteration's pending exception, observed, and popped before re-entry.
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  const std::vector<std::uint8_t> bytes = {
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 28,
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-10),
      op(abc::Opcode::kSTA_V8), 0, op(abc::Opcode::kNEWLEXENV_IMM8), 1,
      op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
      op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 0,
      op(abc::Opcode::kPOPLEXENV),
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-30),
      op(abc::Opcode::kRETURNUNDEFINED)};
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {6}; region.length = 4; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {12}; handler.length = 20;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 1, 3, false, output, ScopeKind::kCatch);
}
}
int main(int argc, char** argv) {
  const bool emit = argc == 2 && std::string_view(argv[1]) == "--emit";
  if (emit) std::cout << "export const functions = [\n";
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                            abc::ProfileId::kDynamic1301}) {
    LoopCarried(profile, emit ? &std::cout : nullptr);
    RetryCarried(profile, emit ? &std::cout : nullptr);
    IterationCell(profile, emit ? &std::cout : nullptr);
    CatchCell(profile, emit ? &std::cout : nullptr);
  }
  if (emit) std::cout << "];\n";
}
