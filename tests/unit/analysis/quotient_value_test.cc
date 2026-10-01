#include <bit>
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
abc::MethodBody Quotient(std::uint64_t left, std::uint64_t right,
                         std::uint32_t count = 1) {
  std::vector<std::uint8_t> code;
  Float(code, left);
  code.insert(code.end(), {static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0});
  for (std::uint32_t i = 0; i < count; ++i) {
    Float(code, right);
    code.insert(code.end(), {static_cast<std::uint8_t>(abc::Opcode::kDIV2_IMM8_V8), 0, 0,
        static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0});
  }
  code.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURN));
  return test::Body(code);
}
abc::MethodBody PhiQuotient(std::uint64_t first, std::uint64_t second) {
  std::vector<std::uint8_t> code{static_cast<std::uint8_t>(abc::Opcode::kLDTRUE)};
  const auto branch = code.size();
  code.insert(code.end(), {static_cast<std::uint8_t>(abc::Opcode::kJEQZ_IMM8), 0});
  Float(code, first);
  code.insert(code.end(), {static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0});
  const auto jump = code.size();
  code.insert(code.end(), {static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), 0});
  code[branch + 1] = static_cast<std::uint8_t>(code.size() - branch);
  Float(code, second);
  code.insert(code.end(), {static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0});
  code[jump + 1] = static_cast<std::uint8_t>(code.size() - jump);
  Float(code, UINT64_C(0x7ff0000000000000)); // Positive infinity, not a finite witness.
  code.insert(code.end(), {static_cast<std::uint8_t>(abc::Opcode::kDIV2_IMM8_V8), 0, 0,
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
  // A throwing division's normal result never appears on its own catch edge.
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
    if (!binary || binary->op != BinaryOperator::kDivide) continue;
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
  constexpr std::uint64_t kSign = UINT64_C(0x8000000000000000);
  constexpr std::uint64_t kInfinity = UINT64_C(0x7ff0000000000000);
  constexpr std::uint64_t kSignalingPayload = UINT64_C(0xfff7000000000005);
  const auto bits = [](double value) { return std::bit_cast<std::uint64_t>(value); };
  {
    const auto integers = test::Body({static_cast<std::uint8_t>(abc::Opcode::kLDAI_IMM32),
        64, 0, 0, 0, static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0,
        static_cast<std::uint8_t>(abc::Opcode::kLDAI_IMM32), 2, 0, 0, 0,
        static_cast<std::uint8_t>(abc::Opcode::kDIV2_IMM8_V8), 0, 0,
        static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
    test::Check(NormalResult(integers) == ValueKinds::NonNaNNumber());
    Phased(integers, true);
    Source(integers);
  }
  for (const auto second : {bits(-2.5), kInfinity, kSignalingPayload}) {
    const bool admitted = second == bits(-2.5);
    const auto body = PhiQuotient(bits(1.0), second);
    test::Check(NormalResult(body) == (admitted ? ValueKinds::NonNaNNumber() : ValueKinds::Unknown()));
    Phased(body, admitted);
  }
  for (const auto count : {1U, 2U, 200U, 201U, 400U, 800U}) {
    const auto body = Quotient(bits(64.0), bits(2.0), count);
    test::Check(NormalResult(body) == ValueKinds::NonNaNNumber());
    Phased(body, true);
  }
  // Zero and infinity may be inputs, but one finite/nonzero input is needed.
  const std::array cases{
      std::pair{bits(64.0), bits(2.5)}, std::pair{bits(-7.0), bits(2.0)},
      std::pair{kSign, bits(2.0)}, std::pair{bits(1.0), kSign},
      std::pair{bits(1.0), kInfinity}, std::pair{kInfinity | kSign, bits(2.0)},
      std::pair{UINT64_C(1), bits(2.0)}};
  if (argc > 1) std::cout << "export default [\n";
  for (const auto [left, right] : cases) {
    const auto body = Quotient(left, right);
    test::Check(NormalResult(body) == ValueKinds::NonNaNNumber());
    Phased(body, true);
    const auto text = Source(body);
    if (argc > 1) std::cout << '(' << text << "),\n";
  }
  if (argc > 1) std::cout << "];\n";
  for (const auto [left, right] : {std::pair{kInfinity, kInfinity},
      std::pair{UINT64_C(0), UINT64_C(0)},
      std::pair{kSignalingPayload, bits(1.0)}, std::pair{bits(1.0), kSignalingPayload},
      std::pair{UINT64_C(0xffff000000000005), bits(1.0)}}) {
    const auto body = Quotient(left, right);
    test::Check(NormalResult(body) == ValueKinds::Unknown());
    Phased(body, false);
    const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, 1));
    test::Check(std::get<SemanticProofFailure>(VerifySemanticSource(body, input, 1, 1)).kind ==
        SemanticProofError::kInternalValue);
  }
}
