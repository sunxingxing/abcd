#include <bit>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "analysis/ssa_fixture.h"
#include "abcd/analysis/value_import.h"
#include "abcd/source/tree_printer.h"
#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/analysis/value_flow.h"
#include "../../../src/source/function_tree.h"

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
void Phased(const abc::MethodBody& original, bool admitted) {
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
std::string Source(const abc::MethodBody& body) {
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
  const auto tree = std::get<ConstructedFunction>(BuildFunctionTree(graph, control, evaluation,
      syntax, {constants})).tree;
  std::ostringstream stream;
  const auto printed = std::get<PrintedFunction>(PrintFunctionTree(tree, stream));
  test::Check(!printed.incomplete);
  return stream.str();
}
}  // namespace
int main(int argc, char**) {
  const auto zero = ValueKinds::FiniteNumber().WithoutInt32MinTag();
  const auto finite = ValueKinds::FiniteNonzeroNumber().WithoutInt32MinTag();
  const auto merged = zero.Join(finite);
  test::Check(merged.IsFiniteNumber() && !merged.IsFiniteNonzeroNumber() && merged.ExcludesInt32MinTag());
  test::Check(!finite.Join(ValueKinds::FiniteNonzeroNumber()).ExcludesInt32MinTag());
  test::Check(merged.Join(ValueKinds::NonNaNNumber()) == ValueKinds::NonNaNNumber());
  test::Check(merged.Join(ValueKinds::String()) == ValueKinds::Defined());
  const std::array kinds{ValueKinds{}, zero, finite, ValueKinds::FiniteNumber(), ValueKinds::FiniteNonzeroNumber(),
      ValueKinds::NonNaNNumber(), ValueKinds::String(), ValueKinds::Defined(), ValueKinds::Language(),
      ValueKinds::Hole(), ValueKinds::Environment(), ValueKinds::OtherInternal(), ValueKinds::Unknown()};
  for (const auto a : kinds) for (const auto b : kinds) {
    test::Check(a.Join(a) == a && a.Join(b) == b.Join(a));
    for (const auto c : kinds) test::Check(a.Join(b).Join(c) == a.Join(b.Join(c)));
  }
  constexpr std::uint64_t kSign = UINT64_C(0x8000000000000000);
  constexpr std::uint64_t kInfinity = UINT64_C(0x7ff0000000000000);
  const auto bits = [](double value) { return std::bit_cast<std::uint64_t>(value); };
  const std::array seeds{UINT64_C(0), kSign, UINT64_C(1), kSign | UINT64_C(1), bits(1.0), bits(-1.0),
      bits(-2147483648.0), UINT64_C(0x7fefffffffffffff), UINT64_C(0xffefffffffffffff),
      kInfinity, kSign | kInfinity, UINT64_C(0x7ff8000000000000), UINT64_C(0xfff7000000000005)};
  for (const auto conversion : {abc::Opcode::kNOP, abc::Opcode::kTONUMERIC_IMM8, abc::Opcode::kTONUMBER_IMM8,
      abc::Opcode::kDEC_IMM8}) {
    const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
    const std::uint32_t seed = static_cast<std::uint32_t>(conversion == abc::Opcode::kDEC_IMM8 ? INT32_MIN + 1 : INT32_MIN);
    std::vector<std::uint8_t> code{op(abc::Opcode::kLDAI_IMM32), 0, 0, 0, 0,
        op(abc::Opcode::kSTA_V8), 0, op(abc::Opcode::kLDAI_IMM32)};
    constexpr unsigned kByteBits = 8;
    for (unsigned shift = 0; shift < sizeof(seed) * kByteBits; shift += kByteBits)
      code.push_back(static_cast<std::uint8_t>(seed >> shift));
    code.push_back(op(conversion));
    if (conversion != abc::Opcode::kNOP) code.push_back(0);
    code.insert(code.end(), {op(abc::Opcode::kSUB2_IMM8_V8), 0, 0, op(abc::Opcode::kRETURN)});
    const auto body = test::Body(code);
    test::Check(NormalResult(body) == ValueKinds::Unknown()); Phased(body, false);
  }
  if (argc > 1) std::cout << "export default [\n";
  for (const auto binary : {abc::Opcode::kADD2_IMM8_V8, abc::Opcode::kSUB2_IMM8_V8})
    for (const auto left : seeds) for (const auto right : seeds) {
      const auto a = std::bit_cast<double>(left), b = std::bit_cast<double>(right);
      const bool admitted = !std::isnan(a) && !std::isnan(b) && (std::isfinite(a) || std::isfinite(b));
      const auto body = Arithmetic(left, right, binary);
      test::Check(NormalResult(body) == (admitted ? ValueKinds::NonNaNNumber() : ValueKinds::Unknown()));
      Phased(body, admitted);
      if (admitted) {
        const auto source = Source(body);
        if (argc > 1) std::cout << "{left:'" << std::hex << left << "',right:'" << right << std::dec
          << "',subtract:" << (binary == abc::Opcode::kSUB2_IMM8_V8 ? "true" : "false") << ",fn:" << source << "},\n";
      }
    }
  if (argc > 1) std::cout << "];\n";
}
