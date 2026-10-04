#include "../../../src/source/function_tree.h"
#include "../../../src/source/logical_regions.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_checks.h"
#include "../../../src/source/family_print.h"
#include "../../../src/source/instruction_sources.h"
#include "../../support/analysis/ssa_fixture.h"
#include "../../support/instruction_positions.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include <nlohmann/json.hpp>
#include <sstream>
using namespace abcd;
using namespace abcd::source;
using test::Check;
namespace {
enum class Form { kDirect, kReversedOperands, kNegated, kMissingUndefined, kDifferentResult, kBarrier,
                  kCopiedSplit, kBooleanGuard, kCallProducer };
abc::MethodBody Body(unsigned count, Form form) {
  std::vector<std::uint8_t> bytes;
  const auto emit = [&](abc::Opcode opcode) {
    const auto wire = static_cast<std::uint16_t>(opcode);
    bytes.push_back(static_cast<std::uint8_t>(wire));
    if (wire > UINT8_MAX) bytes.push_back(static_cast<std::uint8_t>(wire >> 8));
  };
  const auto load = [&](std::uint8_t slot) { emit(abc::Opcode::kLDA_V8); bytes.push_back(slot); };
  const auto store = [&](std::uint8_t slot) { emit(abc::Opcode::kSTA_V8); bytes.push_back(slot); };
  const auto read = [&](std::uint8_t name) {
    load(2); emit(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.insert(bytes.end(), {0, name, 0});
  };
  const auto jump = [&](abc::Opcode opcode) {
    const auto pc = bytes.size(); emit(opcode); const auto operand = bytes.size();
    bytes.insert(bytes.end(), sizeof(std::int32_t), 0); return std::pair{pc, operand};
  };
  const auto patch = [&](auto branch, std::size_t target) {
    const auto delta = static_cast<std::uint32_t>(target - branch.first);
    for (unsigned i = 0; i < sizeof(delta); ++i) bytes[branch.second + i] = static_cast<std::uint8_t>(delta >> (i * 8));
  };
  read(0);
  if (form == Form::kCallProducer) {
    emit(abc::Opcode::kCALLTHIS0_IMM8_V8); bytes.insert(bytes.end(), {0, 2});
  }
  store(0);
  std::vector<std::pair<std::size_t, std::size_t>> exits;
  for (unsigned i = 0; i < count; ++i) for (const bool null : {true, false}) {
    if (!null && form == Form::kMissingUndefined) continue;
    if (form == Form::kBarrier && !null) emit(abc::Opcode::kDEBUGGER);
    if (form == Form::kCopiedSplit) {
      load(0); store(1);
      const auto split = jump(abc::Opcode::kJMP_IMM32); patch(split, bytes.size());
    }
    if (form == Form::kBooleanGuard) {
      emit(abc::Opcode::kLDNULL); emit(abc::Opcode::kSTRICTEQ_IMM8_V8);
      bytes.insert(bytes.end(), {0, 0}); store(1);
    }
    emit(null ? abc::Opcode::kLDNULL : abc::Opcode::kLDUNDEFINED);
    std::uint8_t slot = form == Form::kCopiedSplit || form == Form::kBooleanGuard ? 1 : 0;
    if (form == Form::kReversedOperands) { store(1); load(0); slot = 1; }
    emit(form == Form::kNegated ? abc::Opcode::kSTRICTEQ_IMM8_V8 : abc::Opcode::kSTRICTNOTEQ_IMM8_V8);
    bytes.insert(bytes.end(), {0, slot});
    if (form == Form::kNegated) emit(abc::Opcode::kISFALSE);
    exits.push_back(jump(abc::Opcode::kJEQZ_IMM32));
  }
  if (form == Form::kDifferentResult) read(2); else load(0);
  const auto skip = jump(abc::Opcode::kJMP_IMM32);
  const auto fallback = bytes.size(); read(1);
  for (const auto exit : exits) patch(exit, fallback);
  patch(skip, bytes.size()); emit(abc::Opcode::kRETURN);
  return test::Body(bytes);
}
std::optional<std::string> Source(unsigned count, Form form, abc::ProfileId profile) {
  const auto body = Body(count, form);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(2, 1, {}));
  RouteInputIdentity identity{}; identity.profile = profile;
  const auto input_result = FunctionInput::Build(identity, {}, {}, body, frame, InputCaptureMode::kClosed);
  Check(std::holds_alternative<FunctionInput>(input_result)); const auto& input = std::get<FunctionInput>(input_result);
  auto graph = std::get<analysis::ValueProgram>(analysis::ImportNormalValues(body, input.Input(), input.Proof(), frame));
  Check(!std::holds_alternative<analysis::ValueSimplifyError>(analysis::SimplifyValues(graph, graph.revision)));
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation_result = BuildEvaluationPlan(graph);
  Check(std::holds_alternative<EvaluationPlan>(evaluation_result)); const auto& evaluation = std::get<EvaluationPlan>(evaluation_result);
  Check(!std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, evaluation)));
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.callable = CallableKind::kFunction;
  syntax.strictness = Strictness::kStrict; syntax.names = {u"value", u"probe", u"fallback", u"other"};
  syntax.parameters.push_back({NameId{0}, NameEvidence::kDebugRange});
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (const auto* read = std::get_if<analysis::ValueNamedRead>(&graph.ops[i].operation)) constants[i] = NameId{read->name.value + 1};
  const ExpressionConstants view{constants, 4, 0, {}, {}};
  TreeCheckBudget projection_budget({});
  const auto regions = FindSelectionExpressions(graph, control, evaluation, projection_budget);
  if (form == Form::kBarrier) {
    // Statement-only conditional prefixes are a recorded existing tree gap.
    // They must never gain a native logical projection by erasing the barrier;
    // no generated-source behavior result is credited for these negative cases.
    for (const auto& region : regions) Check(!region.logical);
  }
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), view);
  if (form == Form::kBarrier) {
    Check(std::holds_alternative<FunctionTreeError>(built));
    Check(std::get<FunctionTreeError>(built) == FunctionTreeError::kOwnership);
    return {};
  }
  Check(std::holds_alternative<ConstructedFunction>(built)); const auto& tree = std::get<ConstructedFunction>(built).tree;
  const auto meaning = VerifyTreeSemantics(graph, control, evaluation, tree, view);
  if (const auto* error = std::get_if<TreeSemanticError>(&meaning)) {
    std::cerr << "profile=" << unsigned(profile) << " count=" << count << " form=" << unsigned(form)
              << " semantics=" << unsigned(*error) << '\n';
    const auto flow = VerifyTreeFlow(graph, control, evaluation, tree);
    if (const auto* failure = std::get_if<TreeFlowError>(&flow)) std::cerr << "flow=" << unsigned(*failure) << '\n';
    const auto family = std::get<ConstructedFamily>(BuildFunctionFamily({{tree, {}, {}}}, tree.owner));
    (void)PrintFamilyDraft(family, std::cerr);
  }
  Check(std::holds_alternative<TreeSemantics>(meaning));
  if (form == Form::kDirect && count == 1) {
    bool checked = false;
    for (std::size_t i = 0; i < tree.expressions.size(); ++i) {
      const auto* logical = std::get_if<LogicalExpr>(&tree.expressions[i].expression);
      if (!logical || logical->operation != TreeLogical::kNullish) continue;
      auto wrong = tree;
      std::get<LogicalExpr>(wrong.expressions[i].expression).operation = TreeLogical::kOr;
      Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong, view)));
      wrong = tree;
      auto& reversed = std::get<LogicalExpr>(wrong.expressions[i].expression);
      std::swap(reversed.left, reversed.right);
      Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong, view)));
      Check(std::holds_alternative<AssignmentExpr>(tree.expressions[logical->left.value].expression));
      wrong = tree;
      auto& save = std::get<AssignmentExpr>(wrong.expressions[logical->left.value].expression);
      save.value = logical->right;
      Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong, view)));
      checked = true;
    }
    Check(checked);
  }
  const auto sources = std::get<InstructionSources>(BuildInstructionSources(input, graph, tree));
  const auto family = std::get<ConstructedFamily>(BuildFunctionFamily({{tree, {}, {}}}, tree.owner));
  std::ostringstream source; const auto printed = PrintFamilyDraft(family, source);
  Check(std::holds_alternative<PrintedFamilyDraft>(printed)); const auto& positions = std::get<PrintedFamilyDraft>(printed);
  test::CheckInstructionPositions(tree, sources, positions.members.front(), positions.functions.front());
  const bool expected = form <= Form::kNegated || form == Form::kCopiedSplit || form == Form::kCallProducer;
  Check((source.str().find("??") != std::string::npos) == expected);
  return source.str();
}
}
int main() {
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto count : {1u, 200u, 201u, 402u}) for (unsigned form = 0; form <= unsigned(Form::kCallProducer); ++form) {
      const auto source = Source(count, Form(form), profile);
      std::cout << nlohmann::json{{"profile", unsigned(profile)}, {"count", count}, {"form", form},
          {"source", source ? nlohmann::json(*source) : nlohmann::json(nullptr)}} << '\n';
    }
}
