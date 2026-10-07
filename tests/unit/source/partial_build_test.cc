#include "../../../src/source/function_values.h"
#include "../../../src/source/function_tree.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"

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
std::variant<ConstructedFunction, FunctionTreeError> Build(
    std::initializer_list<std::uint8_t> bytes, RecoveryMode recovery, bool valid_constants = true) {
  const auto body = test::Body(bytes);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {false, false, false, false}));
  RouteInputIdentity identity;
  identity.profile = abc::ProfileId::kDynamic1206;
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
  return BuildFunctionTree(values.graph, control, evaluation,
      syntax, {constants, 0, 0}, {}, nullptr, recovery);
}
std::string Print(const FunctionTree& tree) {
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  return output.str();
}
void DecodedPrefix() {
  // Deprecated NEG still needs a numeric/tag-exclusion proof. Conversion now recovers;
  // preserve the partial-output boundary on an actual unsupported domain.
  const auto bytes = {kDebugger, kLoad, std::uint8_t{0}, kNegate, kNegateHigh, std::uint8_t{0}, kReturnUndefined};
  const auto complete = Build(bytes, RecoveryMode::kCompleteOnly);
  test::Check(std::holds_alternative<FunctionTreeError>(complete));
  test::Check(std::get<FunctionTreeError>(complete) == FunctionTreeError::kExpression);
  const auto recovered = Build(bytes, RecoveryMode::kAllowPartial);
  test::Check(std::holds_alternative<ConstructedFunction>(recovered));
  test::Check(!std::get<ConstructedFunction>(recovered).flow);
  const auto& tree = std::get<ConstructedFunction>(recovered).tree;
  std::ostringstream output;
  const auto printed = PrintFunctionTree(tree, output);
  test::Check(std::holds_alternative<PrintedFunction>(printed));
  test::Check(std::get<PrintedFunction>(printed).incomplete);
  test::Check(output.str().find("debugger;") < output.str().find("UNRECOVERED"));
  test::Check(output.str().find("return") == std::string::npos);
  test::Check(output.str().find("undefined") == std::string::npos);
  test::Check(tree.recovery_issues.size() == 1 && tree.recovery_issues[0].begin.has_value());
  // lda's parameter copy is elided; the first retained gap is deprecated NEG.
  test::Check(tree.recovery_issues[0].begin == abc::CodeOffset{3});
  test::Check(std::holds_alternative<FunctionTreeError>(Build(bytes, RecoveryMode::kAllowPartial, false)));
}
void InlineBoundary() {
  const auto recovered = Build({kDebugger, kLoad, 0, kTypeof, 0, kStore, 0,
      kNegate, kNegateHigh, 0, kReturnUndefined}, RecoveryMode::kAllowPartial);
  test::Check(std::holds_alternative<ConstructedFunction>(recovered));
  test::Check(!std::get<ConstructedFunction>(recovered).flow);
  const auto& tree = std::get<ConstructedFunction>(recovered).tree;
  const auto source = Print(tree);
  test::Check(source.find("debugger;") < source.find("UNRECOVERED"));
  test::Check(source.find("return") == std::string::npos);
  // The unsupported negation consumes the unfinished typeof expression.
  // No orphan operand or stand-alone numeric expression may survive rollback.
  test::Check(tree.expressions.empty());
  test::Check(tree.recovery_issues[0].begin == abc::CodeOffset{3});
}
void RetainedCall() {
  const auto recovered = Build({kLoad, 0, kCall, 0, kDebugger, kLoad, 0,
      kNegate, kNegateHigh, 0, kReturnUndefined}, RecoveryMode::kAllowPartial);
  test::Check(std::holds_alternative<ConstructedFunction>(recovered));
  test::Check(!std::get<ConstructedFunction>(recovered).flow);
  const auto& tree = std::get<ConstructedFunction>(recovered).tree;
  const auto source = Print(tree);
  test::Check(source.find("(0, ") < source.find("debugger;"));
  test::Check(source.find("debugger;") < source.find("UNRECOVERED"));
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
  test::Check(std::holds_alternative<FunctionTreeError>(branch));
  test::Check(std::get<FunctionTreeError>(branch) == FunctionTreeError::kExpression);
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2 && std::string_view{argv[1]} == "--emit-complete") {
    auto built = Build({kDebugger, kInteger, 7, 0, 0, 0, kReturn}, RecoveryMode::kAllowPartial);
    test::Check(std::holds_alternative<ConstructedFunction>(built));
    std::cout << Print(std::get<ConstructedFunction>(built).tree);
    return 0;
  }
  DecodedPrefix(); InlineBoundary(); RetainedCall(); CompleteAndBranched();
}
