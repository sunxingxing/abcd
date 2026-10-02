#include <bit>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

#include "analysis/ssa_fixture.h"
#include "abcd/analysis/value_import.h"
#include "abcd/source/tree_printer.h"
#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/analysis/value_flow.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/tree_semantics.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
void Float(std::vector<std::uint8_t>& code, std::uint64_t bits) {
  code.push_back(static_cast<std::uint8_t>(abc::Opcode::kFLDAI_IMM64));
  for (unsigned byte = 0; byte < sizeof(bits); ++byte)
    code.push_back(static_cast<std::uint8_t>(bits >> (8 * byte)));
}
abc::MethodBody Arithmetic(std::uint64_t left, std::uint64_t right, abc::Opcode binary) {
  std::vector<std::uint8_t> code; Float(code, left);
  code.insert(code.end(), {static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0}); Float(code, right);
  code.insert(code.end(), {static_cast<std::uint8_t>(binary), 0, 0,
      static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
  return test::Body(code);
}
ValueKinds NormalResult(const abc::MethodBody& body) {
  IdfQuota quota{UINT64_MAX};
  const auto ssa = std::get<RegisterSsa>(RegisterSsa::Build(
      body, std::get<NormalFlow>(BuildNormalFlow(body)), 1, quota));
  const auto values = std::get<RegisterValueFlow>(RegisterValueFlow::Build(body, ssa, 1, {}, quota));
  return *values.At(ssa.Reads({static_cast<std::uint32_t>(body.instructions.size() - 1)})[0].value);
}
void Phased(const abc::MethodBody& original, bool admitted, bool representable = true) {
  auto body = original;
  const auto return_pc = body.instructions.back().offset;
  // An arithmetic operation's normal result never appears on its own catch edge.
  body.instructions.push_back(test::Body({static_cast<std::uint8_t>(abc::Opcode::kRETURN)}).instructions[0]);
  body.instructions.back().offset = {return_pc.value + 1};
  abc::TryRegion region;
  region.length = return_pc.value;
  region.handler_count = 1;
  body.tries.push_back(region);
  abc::CatchHandler handler;
  handler.offset = body.instructions.back().offset;
  handler.length = 1;
  body.handlers.push_back(handler);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(1, 0, {}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, abc::ProfileId::kDynamic1206,
      frame.registers, {}, {}, &frame));
  IdfQuota quota{UINT64_MAX};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
  const auto environments = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, {}, quota,
      {}, PendingExceptionValues::kLanguage));
  const auto checked = VerifyExceptionInputs(flow, ssa, environments, values, UINT64_MAX);
  if (!admitted) {
    test::Check(std::get<ExceptionInputFailure>(checked).kind == ExceptionInputError::kInternalValue);
    return;
  }
  test::Check(std::holds_alternative<ExceptionInputProof>(checked));
  const PhasedValueInput input{flow, ssa, environments, values,
      std::get<ExceptionInputProof>(checked), frame};
  const auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  test::Check(std::holds_alternative<PhasedImportProof>(VerifyImportedPhases(graph, input)));
  test::Check(std::holds_alternative<PhasedValueFlowProof>(VerifyPhasedValueFlow(graph)));
  using namespace abcd::source;
  const auto evaluation = std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));
  const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
  const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
  FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  std::vector<ExpressionConstant> constants(graph.ops.size()); const ExpressionConstants resolved{constants};
  const ExceptionTreeInput context{body, input, regions, control};
  const auto built = BuildExceptionFunctionDraft(graph, context, evaluation, syntax, resolved);
  if (!representable) {
    test::Check(std::holds_alternative<FunctionTreeError>(built));
    test::Check(std::get<FunctionTreeError>(built) == FunctionTreeError::kSemantics);
    return;
  }
  test::Check(std::holds_alternative<ExceptionFunctionDraft>(built));
  const auto& tree = std::get<ExceptionFunctionDraft>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(graph, context, evaluation, tree, resolved)));

  for (const auto& op : graph.ops) {
    const auto* binary = std::get_if<ValueBinary>(&op.operation);
    if (!binary) continue;
    test::Check(op.result && op.exceptional == ExceptionalCompletion::kMayThrow);
    const auto result = std::get<OpResult>(graph.values[op.result->value].origin);
    test::Check(result.commit && graph.phases[result.commit->value].phase == ExceptionPhase::kCommit);
    // The result fact does not erase conversion effects or exception routing.
    test::Check(op.normal == LanguageEffect::kUserCode && op.before_throw == LanguageEffect::kUserCode);
  }
}
std::string Source(const abc::MethodBody& body, bool representable = true) {
  using namespace abcd::source;
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(1, 0, {}));
  const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body,
      abc::ProfileId::kDynamic1206, frame.registers));
  const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, input, frame.registers, frame.locals));
  const auto graph = std::get<ValueProgram>(ImportNormalValues(body, input, proof, frame));
  test::Check(!VerifyImportedValues(graph, input, proof, frame));
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  std::vector<ExpressionConstant> constants(graph.ops.size());
  FunctionSyntax syntax{};
  syntax.callable = CallableKind::kFunction;
  syntax.strictness = Strictness::kStrict;
  const auto built = BuildFunctionTree(graph, control, evaluation, syntax, {constants});
  if (!representable) {
    test::Check(std::holds_alternative<FunctionTreeError>(built));
    test::Check(std::get<FunctionTreeError>(built) == FunctionTreeError::kSemantics);
    return {};
  }
  const auto tree = std::get<ConstructedFunction>(built).tree;
  std::ostringstream stream;
  const auto printed = std::get<PrintedFunction>(PrintFunctionTree(tree, stream));
  test::Check(!printed.incomplete);
  return stream.str();
}
}  // namespace
int main(int argc, char**) {
  const auto integral = ValueKinds::FiniteNumber().WithIntegralNumber();
  test::Check(integral.IsIntegralNumber());
  test::Check(!integral.Join(ValueKinds::FiniteNumber()).IsIntegralNumber());
  test::Check(integral.Join(ValueKinds::FiniteNonzeroNumber().WithIntegralNumber()).IsIntegralNumber());
  test::Check(!ValueKinds::NonNaNNumber().WithIntegralNumber().IsIntegralNumber());
  const std::array kinds{ValueKinds{}, integral, integral.WithoutInt32MinTag(),
      ValueKinds::FiniteNonzeroNumber().WithIntegralNumber(), ValueKinds::FiniteNumber(),
      ValueKinds::FiniteNonzeroNumber(), ValueKinds::NonNaNNumber(), ValueKinds::String(),
      ValueKinds::Defined(), ValueKinds::Language(), ValueKinds::Hole(), ValueKinds::Unknown()};
  for (const auto a : kinds) for (const auto b : kinds) {
    test::Check(a.Join(a) == a && a.Join(b) == b.Join(a));
    for (const auto c : kinds) test::Check(a.Join(b).Join(c) == a.Join(b.Join(c)));
  }
  constexpr std::uint64_t kSign = UINT64_C(0x8000000000000000);
  constexpr std::uint64_t kInfinity = UINT64_C(0x7ff0000000000000);
  const auto bits = [](double value) { return std::bit_cast<std::uint64_t>(value); };
  const std::array bases{UINT64_C(0), kSign, UINT64_C(1), kSign | UINT64_C(1), bits(1.0), bits(-1.0),
      bits(-2.0), bits(2.0), bits(-2147483648.0), UINT64_C(0x7fefffffffffffff), UINT64_C(0xffefffffffffffff),
      kInfinity, kSign | kInfinity, UINT64_C(0x7ff8000000000000), UINT64_C(0xfff7000000000005)};
  const std::array exponents{UINT64_C(0), kSign, bits(1.0), bits(-1.0), bits(2.0), bits(-2.0), bits(3.0),
      bits(-3.0), bits(0.5), bits(-0.5), bits(2147483647.0), bits(-2147483648.0),
      bits(9007199254740992.0), kInfinity, kSign | kInfinity, UINT64_C(0x7ff8000000000000)};
  if (argc > 1) std::cout << "export default [\n";
  for (const auto left : bases) for (const auto right : exponents) {
    const auto a = std::bit_cast<double>(left), b = std::bit_cast<double>(right);
    const bool admitted = !std::isnan(a) && std::isfinite(b) && std::trunc(b) == b;
    const auto body = Arithmetic(left, right, abc::Opcode::kEXP_IMM8_V8);
    test::Check(NormalResult(body) == (admitted ? ValueKinds::NonNaNNumber() : ValueKinds::Unknown()));
    Phased(body, admitted);
    if (admitted) {
      const auto source = Source(body);
      if (argc > 1) std::cout << "{left:'" << std::hex << left << "',right:'" << right << std::dec
        << "',fn:" << source << "},\n";
    }
  }
  constexpr std::array bitwise{abc::Opcode::kSHL2_IMM8_V8, abc::Opcode::kSHR2_IMM8_V8,
      abc::Opcode::kASHR2_IMM8_V8, abc::Opcode::kAND2_IMM8_V8, abc::Opcode::kOR2_IMM8_V8, abc::Opcode::kXOR2_IMM8_V8};
  for (std::size_t index = 0; index < bitwise.size(); ++index)
    for (const auto left : bases) for (const auto right : {bits(0.0), bits(31.0), UINT64_C(0x7ff8000000000000)}) {
      const bool unsigned_shift = bitwise[index] == abc::Opcode::kSHR2_IMM8_V8;
      const bool known_number = !std::isnan(std::bit_cast<double>(left)) || !std::isnan(std::bit_cast<double>(right));
      auto expected = unsigned_shift || known_number ? ValueKinds::FiniteNumber().WithIntegralNumber() : ValueKinds::Defined();
      if (unsigned_shift) expected = expected.WithoutInt32MinTag();
      const auto body = Arithmetic(left, right, bitwise[index]);
      test::Check(NormalResult(body) == expected);
      // The result proof does not make a noncanonical input NaN literal
      // representable in source. Keep that independent tree boundary explicit.
      const bool representable = !std::isnan(std::bit_cast<double>(left)) ||
          left == std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN());
      Phased(body, true, representable);
      const auto source = Source(body, representable);
      constexpr unsigned kFirstBitwiseOracle = 11;
      if (argc > 1 && representable) std::cout << "{left:'" << std::hex << left << "',right:'" << right << std::dec
        << "',binary:" << kFirstBitwiseOracle + index << ",fn:" << source << "},\n";
    }
  for (const auto unary : {abc::Opcode::kINC_IMM8, abc::Opcode::kDEC_IMM8,
      abc::Opcode::kDEPRECATED_INC_PREF_V8, abc::Opcode::kDEPRECATED_DEC_PREF_V8})
    for (const auto seed : bases) {
      const auto number = std::bit_cast<double>(seed);
      std::vector<std::uint8_t> code; Float(code, seed);
      const auto encoded = static_cast<std::uint16_t>(unary);
      constexpr unsigned kByteBits = 8;
      if (encoded > UINT8_MAX) {
        code.insert(code.end(), {static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0,
            static_cast<std::uint8_t>(abc::Opcode::kLDAI_IMM32), 9, 0, 0, 0,
            static_cast<std::uint8_t>(encoded), static_cast<std::uint8_t>(encoded >> kByteBits), 0});
      } else code.insert(code.end(), {static_cast<std::uint8_t>(encoded), 0});
      code.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURN));
      const auto body = test::Body(code);
      auto expected = std::isnan(number) ? ValueKinds::Unknown() : std::isfinite(number)
          ? ValueKinds::FiniteNumber() : ValueKinds::NonNaNNumber();
      if (std::isfinite(number) && std::trunc(number) == number) expected = expected.WithIntegralNumber();
      test::Check(NormalResult(body) == expected);
      Phased(body, !std::isnan(number));
      if (!std::isnan(number)) {
        const auto source = Source(body);
        const bool increment = unary == abc::Opcode::kINC_IMM8 || unary == abc::Opcode::kDEPRECATED_INC_PREF_V8;
        if (argc > 1) std::cout << "{left:'" << std::hex << seed << std::dec << "',update:"
          << (increment ? 1 : -1) << ",fn:" << source << "},\n";
      }
    }
  if (argc > 1) std::cout << "];\n";
}
