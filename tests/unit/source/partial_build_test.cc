#include "../../../src/source/function_values.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/expression_forest.h"
#include "../../../src/source/tree_semantics.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include "analysis/numeric_proof.h"
#include "abcd/analysis/value_import.h"

#include <sstream>
#include <string_view>

namespace {
using namespace abcd;
using namespace abcd::source;
// Opcode values from pinned runtime_core/isa/isa.yaml, dynamic 12.0.6 profile.
constexpr auto kLegacyNegate = static_cast<std::uint16_t>(abc::Opcode::kDEPRECATED_NEG_PREF_V8);
constexpr std::uint8_t kNegateHigh = static_cast<std::uint8_t>(kLegacyNegate >> 8);
constexpr std::uint8_t kStore = static_cast<std::uint8_t>(abc::Opcode::kSTA_V8);
constexpr std::uint8_t kDebugger = 0xb0, kInteger = 0x62, kNegate = static_cast<std::uint8_t>(kLegacyNegate), kReturn = 0x64,
    kReturnUndefined = 0x65, kLoad = 0x60, kCall = 0x29, kTypeof = 0x1c, kZeroBranch = 0x4f;
std::variant<ConstructedFunction, FunctionTreeFailure> Build(
    std::initializer_list<std::uint8_t> bytes, RecoveryMode recovery, bool valid_constants = true) {
  const auto body = test::Body(bytes);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {false, false, false, false}));
  RouteInputIdentity identity;
  identity.profile = abc::ProfileId::kDynamic1206;
  identity.contract = kSourceContract;  // Source target; raw pinned expression proof is checked independently.
  identity.abc_version = {12, 0, 6, 0};
  auto input_result = FunctionInput::Build(identity, {200}, {100}, body, frame, InputCaptureMode::kConditional);
  if (const auto* failure = std::get_if<FunctionInputFailure>(&input_result))
    std::visit([](const auto& error) {
      if constexpr (requires { error.kind; error.pc; })
        std::cerr << "input cause " << static_cast<unsigned>(error.kind) << " PC " << error.pc.value << '\n';
      else std::cerr << "input error " << static_cast<unsigned>(error) << '\n';
    }, failure->cause);
  test::Check(std::holds_alternative<FunctionInput>(input_result));
  const auto& input = std::get<FunctionInput>(input_result);
  auto value_result = BuildFunctionValues(input, body, frame);
  test::Check(std::holds_alternative<FunctionValues>(value_result));
  const auto& values = std::get<FunctionValues>(value_result);
  auto control_result = BuildControlPlan(values.graph);
  test::Check(std::holds_alternative<ControlPlan>(control_result));
  const auto& control = std::get<ControlPlan>(control_result);
  auto evaluation_result = BuildEvaluationPlan(values.graph);
  test::Check(std::holds_alternative<EvaluationPlan>(evaluation_result));
  const auto& evaluation = std::get<EvaluationPlan>(evaluation_result);
  FunctionSyntax syntax{};
  syntax.identity = identity; syntax.method = {200};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.parameters.resize(frame.parameters);
  std::vector<ExpressionConstant> constants(values.graph.ops.size());
  if (!valid_constants) constants.pop_back();
  if (valid_constants && std::any_of(values.graph.ops.begin(), values.graph.ops.end(), [](const auto& op) {
        const auto* unary = std::get_if<analysis::ValueUnary>(&op.operation);
        return unary && unary->op == analysis::UnaryOperator::kLegacyNegate;
      })) {
    // Independent pinned expression analysis retains its refusal/partial path.
    const auto method = std::get<analysis::SemanticMethod>(analysis::BuildSemanticMethod(
        body, identity.profile, frame.registers, {}, {}, &frame));
    const auto proof = std::get<analysis::SemanticProof>(test::NumericProof(body, identity.profile, frame));
    const auto graph = std::get<analysis::ValueProgram>(analysis::ImportNormalValues(body, method, proof, frame));
    const auto plan = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
    std::vector<ExpressionConstant> raw_constants(graph.ops.size());
    const auto raw = BuildExpressionForest(graph, plan, frame.parameters, syntax.owner,
        {raw_constants}, {}, {}, RecoveryMode::kCompleteOnly);
    test::Check(std::get<ExpressionFailure>(raw).kind == ExpressionError::kUnsupportedOperation);
    const auto partial = BuildExpressionForest(graph, plan, frame.parameters, syntax.owner,
        {raw_constants}, {}, {}, RecoveryMode::kAllowPartial);
    if (graph.blocks.size() == 1)
      test::Check(std::get<ExpressionForest>(partial).recovery_stop.has_value());
    else
      test::Check(std::get<ExpressionFailure>(partial).kind == ExpressionError::kUnsupportedOperation);
  }
  auto result = BuildFunctionTree(values.graph, control, evaluation,
      syntax, {constants, 0, 0}, {}, nullptr, recovery);
  if (const auto* built = std::get_if<ConstructedFunction>(&result))
    test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(values.graph, control,
        evaluation, built->tree, {constants})));
  return result;
}
std::string Print(const FunctionTree& tree) {
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  return output.str();
}
void DecodedPrefix() {
  const auto bytes = {kDebugger, kLoad, std::uint8_t{0}, kNegate, kNegateHigh, std::uint8_t{0}, kReturnUndefined};
  for (auto recovery : {RecoveryMode::kCompleteOnly, RecoveryMode::kAllowPartial}) {
    const auto built = std::get<ConstructedFunction>(Build(bytes, recovery));
    test::Check(built.flow.has_value() && built.tree.recovery_issues.empty());
    const auto source = Print(built.tree);
    test::Check(source.find("UNRECOVERED") == std::string::npos);
    test::Check(source.find("debugger;") < source.find("-"));
    test::Check(source.find("return") == std::string::npos);
  }
  test::Check(std::get<FunctionTreeFailure>(Build(bytes, RecoveryMode::kAllowPartial, false)).kind == FunctionTreeError::kExpression);
}
void InlineBoundary() {
  const auto built = std::get<ConstructedFunction>(Build({kDebugger, kLoad, 0, kTypeof, 0, kStore, 0,
      kNegate, kNegateHigh, 0, kReturnUndefined}, RecoveryMode::kAllowPartial));
  test::Check(built.flow.has_value() && built.tree.recovery_issues.empty());
  const auto source = Print(built.tree);
  test::Check(source.find("UNRECOVERED") == std::string::npos);
  test::Check(source.find("typeof") == std::string::npos);
}
void RetainedCall() {
  const auto built = std::get<ConstructedFunction>(Build({kLoad, 0, kCall, 0, kDebugger, kLoad, 0,
      kNegate, kNegateHigh, 0, kReturnUndefined}, RecoveryMode::kAllowPartial));
  test::Check(built.flow.has_value() && built.tree.recovery_issues.empty());
  const auto source = Print(built.tree);
  test::Check(source.find("();") < source.find("debugger;"));
  test::Check(source.find("debugger;") < source.find("-"));
}
void CompleteAndBranched() {
  const auto complete = Build({kDebugger, kInteger, 7, 0, 0, 0, kReturn}, RecoveryMode::kAllowPartial);
  test::Check(std::holds_alternative<ConstructedFunction>(complete));
  test::Check(std::get<ConstructedFunction>(complete).flow.has_value());
  const auto& tree = std::get<ConstructedFunction>(complete).tree;
  test::Check(tree.recovery_issues.empty());
  test::Check(Print(tree).find("return 7;") != std::string::npos);
  const auto branch = Build({kDebugger, kLoad, 0, kNegate, kNegateHigh, 0, kInteger, 7, 0, 0, 0,
      kZeroBranch, 3, kReturnUndefined, kReturnUndefined}, RecoveryMode::kAllowPartial);
  test::Check(std::get<ConstructedFunction>(branch).tree.recovery_issues.empty());
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2 && std::string_view{argv[1]} == "--emit-negation") {
    std::cout << Print(std::get<ConstructedFunction>(Build({kDebugger, kLoad, 0,
        kNegate, kNegateHigh, 0, kReturnUndefined}, RecoveryMode::kAllowPartial)).tree);
    return 0;
  }
  if (argc == 2 && std::string_view{argv[1]} == "--emit-complete") {
    auto built = Build({kDebugger, kInteger, 7, 0, 0, 0, kReturn}, RecoveryMode::kAllowPartial);
    test::Check(std::holds_alternative<ConstructedFunction>(built));
    std::cout << Print(std::get<ConstructedFunction>(built).tree);
    return 0;
  }
  DecodedPrefix(); InlineBoundary(); RetainedCall(); CompleteAndBranched();
}
