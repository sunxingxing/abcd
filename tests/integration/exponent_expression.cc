#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/instruction_sources.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include <array>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
constexpr unsigned kByteBits = 8;
enum class ExponentInput { kLiteral, kIncrement, kDecrement, kBitNot };
std::uint64_t Run(abc::ProfileId profile, abc::Opcode binary, std::int32_t seed, std::int32_t step,
    unsigned count, bool print = false, bool admitted = true, ExponentInput exponent_input = ExponentInput::kLiteral) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  std::vector<std::uint8_t> bytes;
  const auto constant = [&](std::int32_t value) {
    bytes.push_back(op(abc::Opcode::kLDAI_IMM32));
    const auto bits = static_cast<std::uint32_t>(value);
    for (unsigned shift = 0; shift < sizeof(bits) * kByteBits; shift += kByteBits)
      bytes.push_back(static_cast<std::uint8_t>(bits >> shift));
  };
  constant(seed);
  for (unsigned i = 0; i < count; ++i) {
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 0});
    if (exponent_input == ExponentInput::kBitNot) {
      constant(~step); bytes.insert(bytes.end(), {op(abc::Opcode::kNOT_IMM8), 0});
    } else if (exponent_input != ExponentInput::kLiteral) {
      test::Check(step > INT32_MIN && step < INT32_MAX);
      const bool increment = exponent_input == ExponentInput::kIncrement;
      constant(step + (increment ? -1 : 1));
      bytes.insert(bytes.end(), {op(increment ? abc::Opcode::kINC_IMM8 : abc::Opcode::kDEC_IMM8), 0});
    } else if (step < 0) {
      test::Check(step != INT32_MIN);
      constant(-step); bytes.insert(bytes.end(), {op(abc::Opcode::kNEG_IMM8), 0});
    } else constant(step);
    bytes.insert(bytes.end(), {op(binary), 0, 0});
  }
  bytes.push_back(op(abc::Opcode::kRETURN));
  const auto body = test::Body(bytes);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(1, 0, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto prepared = FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed);
  if (!admitted) { test::Check(!std::holds_alternative<FunctionInput>(prepared)); return 0; }
  test::Check(std::holds_alternative<FunctionInput>(prepared));
  const auto& input = std::get<FunctionInput>(prepared);
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = input.Method(); syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  std::vector<ExpressionConstant> constants(graph.ops.size());
  const ExpressionConstants resolved{constants};
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(input, graph, tree)));
  std::ostringstream out; test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, out)));
  if (print) std::cout << out.str();
  return evaluation.work;
}
}
int main(int argc, char** argv) {
  if (argc == 2) {
    const auto option = std::string_view(argv[1]);
    test::Check(option == "--source" || option == "--increment-source" || option == "--decrement-source" || option == "--bitnot-source");
    const auto input = option == "--bitnot-source" ? ExponentInput::kBitNot : option == "--increment-source" ? ExponentInput::kIncrement :
        option == "--decrement-source" ? ExponentInput::kDecrement : ExponentInput::kLiteral;
    std::cout << "export default [\n";
    for (const auto binary : {abc::Opcode::kEXP_IMM8_V8})
      for (const auto seed : {0, 7, -7, INT32_MIN, INT32_MAX}) for (const auto step : {0, 1, -1, 2, -2, 3}) {
        std::cout << "{seed:" << seed << ",step:" << step << ",fn:";
        Run(abc::ProfileId::kDynamic1206, binary, seed, step, 2, true, true, input);
        std::cout << "},\n";
      }
    std::cout << "];\n"; return 0;
  }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto input : {ExponentInput::kLiteral, ExponentInput::kIncrement, ExponentInput::kDecrement, ExponentInput::kBitNot})
    for (const auto op : {abc::Opcode::kEXP_IMM8_V8}) {
      for (const auto seed : {0, 7, -7, INT32_MIN, INT32_MAX})
        for (const auto step : {0, 1, -1, 2, -2, 3}) Run(profile, op, seed, step, 2, false, true, input);
      std::uint64_t previous = 0;
      for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
        const auto work = Run(profile, op, 0, 1, count, false, true, input);
        if (count == 402) test::Check(work < 3 * previous);
        previous = work;
      }
    }
}
