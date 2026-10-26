#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/instruction_sources.h"
#include "../../src/analysis/phased_value_import.h"
#include "../../src/source/exception_tree.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include "analysis/numeric_proof.h"
#include <array>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
constexpr std::array kOperations{abc::Opcode::kADD2_IMM8_V8, abc::Opcode::kSUB2_IMM8_V8,
    abc::Opcode::kMUL2_IMM8_V8, abc::Opcode::kDIV2_IMM8_V8, abc::Opcode::kMOD2_IMM8_V8,
    abc::Opcode::kEXP_IMM8_V8, abc::Opcode::kSHL2_IMM8_V8, abc::Opcode::kSHR2_IMM8_V8,
    abc::Opcode::kASHR2_IMM8_V8, abc::Opcode::kAND2_IMM8_V8, abc::Opcode::kOR2_IMM8_V8,
    abc::Opcode::kXOR2_IMM8_V8};
abc::MethodBody Body(abc::ProfileId profile, abc::Opcode binary, unsigned form, unsigned count, bool phased, bool private_input = false) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  std::vector<std::uint8_t> bytes;
  // Three local registers, then three independent ABI parameters.
  if (form == 1) bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), 1, 3,
      op(abc::Opcode::kMOV_V8_V8), 2, 4, op(abc::Opcode::kJMP_IMM8), 2});
  if (private_input) bytes.push_back(op(abc::Opcode::kLDHOLE));
  else bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), static_cast<std::uint8_t>(form == 1 ? 1 : 3)});
  for (unsigned i = 0; i < count; ++i) {
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 0,
        op(abc::Opcode::kLDA_V8), static_cast<std::uint8_t>(form == 1 ? 2 : 4)});
    if (form == 2) bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 1,
        op(abc::Opcode::kLDA_V8), 5, op(abc::Opcode::kMUL2_IMM8_V8), 0, 1});
    if (form == 3) bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 1,
        op(abc::Opcode::kLDA_V8), 5, op(binary), 0, 1});
    bytes.insert(bytes.end(), {op(binary), 0, 0});
    if (form == 4) bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 1,
        op(abc::Opcode::kLDA_V8), 5, op(abc::Opcode::kMUL2_IMM8_V8), 0, 1});
  }
  bytes.push_back(op(abc::Opcode::kRETURN));
  const auto catch_pc = static_cast<std::uint32_t>(bytes.size());
  if (phased) bytes.push_back(op(abc::Opcode::kRETURN));
  abc::MethodBody body;
  for (std::uint32_t pc = 0; pc < bytes.size();) {
    const auto decoded = std::get<abc::DecodedInstruction>(abc::DecodeInstruction(std::span(bytes).subspan(pc), profile));
    abc::BodyInstruction instruction{{pc}, decoded, {}};
    if (decoded.branch_delta) instruction.branch_target = abc::CodeOffset{static_cast<std::uint32_t>(pc + *decoded.branch_delta)};
    body.instructions.push_back(instruction); pc += decoded.size;
  }
  if (phased) {
    abc::TryRegion region; region.length = catch_pc - 1; region.handler_count = 1; body.tries.push_back(region);
    abc::CatchHandler handler; handler.offset = {catch_pc}; handler.length = 1; body.handlers.push_back(handler);
  }
  return body;
}
std::uint64_t Run(abc::ProfileId profile, abc::Opcode binary, unsigned form, unsigned count, bool phased, bool print) {
  const auto body = Body(profile, binary, form, count, phased);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(3, 3, {}));
  RouteInputIdentity identity; identity.profile = profile;
  identity.contract = kSourceContract;
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = {300}; syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"left", u"right", u"third"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}, {NameId{1}, NameEvidence::kDebugRange},
      {NameId{2}, NameEvidence::kDebugRange}};
  if (!phased && form == 0 && count == 1) {
    if (binary == kOperations[0]) {
      test::CheckPinnedNumericRefusal(body, profile, frame);
    }
    const auto private_input = Body(profile, binary, form, count, phased, true);
    test::Check(std::holds_alternative<FunctionInputFailure>(FunctionInput::Build(identity,
        {300}, {100}, private_input, frame, InputCaptureMode::kClosed)));
  }
  std::ostringstream output;
  std::uint64_t work = 0;
  if (!phased) {
    const auto prepared = FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed);
    test::Check(std::holds_alternative<FunctionInput>(prepared));
    const auto& input = std::get<FunctionInput>(prepared);
    const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
    const auto& graph = values.graph;
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph)); work = evaluation.work;
    std::vector<ExpressionConstant> constants(graph.ops.size()); const ExpressionConstants resolved{constants, syntax.names.size()};
    const auto tree = std::get<ConstructedFunction>(BuildFunctionTree(graph, control, evaluation, syntax, resolved)).tree;
    test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
    test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(input, graph, tree)));
    auto changed_contract = tree; changed_contract.identity.contract = SourceContract::kLanguageSourceV9;
    test::Check(std::holds_alternative<InstructionSourceError>(BuildInstructionSources(input, graph, changed_contract)));
    test::Check(!std::get<PrintedFunction>(PrintFunctionTree(tree, output)).incomplete);
  } else {
    const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers, {}, {}, &frame));
    IdfQuota quota{kDefaultSemanticWork};
    const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
    std::vector<EntryValueFact> entries;
    for (std::size_t i = 0; i < ssa.Values().size(); ++i) if (std::holds_alternative<MethodEntry>(ssa.Values()[i].origin))
      if (const auto* reg = std::get_if<abc::RegisterId>(&ssa.Values()[i].slot); reg && reg->value >= frame.locals)
        entries.push_back({{static_cast<std::uint32_t>(i)}, ValueKinds::Language()});
    const auto environments = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
    const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries, quota,
        {}, PendingExceptionValues::kLanguage, {}, {}, CallResultValues::kLanguage,
        FrameReceiver::kUnknown, kSourceArithmetic));
    const auto checked = VerifyExceptionInputs(flow, ssa, environments, values);
    test::Check(std::holds_alternative<ExceptionInputProof>(checked));
    const PhasedValueInput input{flow, ssa, environments, values, std::get<ExceptionInputProof>(checked), frame};
    const auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
    test::Check(std::holds_alternative<PhasedImportProof>(VerifyImportedPhases(graph, input)));
    const auto evaluation = std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph)); work = evaluation.work;
    const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
    const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
    std::vector<ExpressionConstant> constants(graph.ops.size()); const ExpressionConstants resolved{constants, syntax.names.size()};
    const ExceptionTreeInput context{body, input, regions, control};
    const auto tree = std::get<ExceptionFunctionDraft>(BuildExceptionFunctionDraft(graph, context, evaluation, syntax, resolved)).tree;
    test::Check(std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(graph, context, evaluation, tree, resolved)));
    test::Check(!std::get<PrintedFunction>(PrintFunctionTree(tree, output)).incomplete);
  }
  if (print) std::cout << output.str();
  return work;
}
}
int main(int argc, char**) {
  if (argc == 1) {
    const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
    const auto body = test::Body({op(abc::Opcode::kLDAI_IMM32), 0, 0, 0, 0,
        op(abc::Opcode::kSTA_V8), 0, op(abc::Opcode::kLDAI_IMM32), 0, 0, 0, 128,
        op(abc::Opcode::kSUB2_IMM8_V8), 0, 0, op(abc::Opcode::kNEG_IMM8), 0, op(abc::Opcode::kRETURN)});
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(1, 0, {}));
    RouteInputIdentity identity; identity.profile = abc::ProfileId::kDynamic1206;
    test::CheckPinnedNumericRefusal(body, identity.profile, frame);
    identity.contract = kSourceContract;
    const auto input = FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed);
    test::Check(std::holds_alternative<FunctionInput>(input));
    const auto& proof = std::get<FunctionInput>(input).Proof();
    test::Check(proof.arithmetic == ArithmeticSemantics::kSourceNumeric);
  }

  if (argc > 1) std::cout << "export default [\n";
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (unsigned op = 0; op < kOperations.size(); ++op) for (unsigned form = 0; form < 5; ++form)
      for (const bool phased : {false, true}) {
        if (argc > 1) std::cout << "{op:" << op << ",form:" << form << ",phased:" << (phased ? "true" : "false") << ",fn:";
        Run(profile, kOperations[op], form, 1, phased, argc > 1);
        if (argc > 1) std::cout << "},\n";
      }
  if (argc > 1) std::cout << "];\n";
  if (argc == 1) for (const bool phased : {false, true}) {
    std::uint64_t previous = 0;
    for (const auto count : {200U, 201U, 402U}) {
      const auto work = Run(abc::ProfileId::kDynamic1206, kOperations[0], 0, count, phased, false);
      if (count == 402) test::Check(work < 3 * previous);
      previous = work;
    }
  }
}
