#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/instruction_sources.h"
#include "../../src/analysis/phased_value_import.h"
#include "../../src/source/exception_tree.h"
#include "abcd/source/tree_printer.h"
#include "abcd/analysis/linear_lexicals.h"
#include "analysis/ssa_fixture.h"
#include <array>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
constexpr unsigned kByteBits = 8;
constexpr std::array kOperations{abc::Opcode::kNEG_IMM8, abc::Opcode::kINC_IMM8,
    abc::Opcode::kDEC_IMM8, abc::Opcode::kTONUMBER_IMM8, abc::Opcode::kTONUMERIC_IMM8,
    abc::Opcode::kDEPRECATED_NEG_PREF_V8, abc::Opcode::kDEPRECATED_INC_PREF_V8,
    abc::Opcode::kDEPRECATED_DEC_PREF_V8, abc::Opcode::kDEPRECATED_TONUMERIC_PREF_V8,
    abc::Opcode::kDEPRECATED_TONUMBER_PREF_V8};
abc::MethodBody Body(abc::ProfileId profile, abc::Opcode unary, unsigned form, unsigned count, bool phased, bool private_input = false) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  std::vector<std::uint8_t> bytes;
  // Independent input copies and block splitting must use the same transfer.
  if (form == 1) bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), 1, 3,
      op(abc::Opcode::kJMP_IMM8), 2});
  if (private_input) bytes.push_back(op(abc::Opcode::kLDHOLE));
  else if (form >= 5) {
    bytes.push_back(op(abc::Opcode::kFLDAI_IMM64));
    const std::uint64_t bits = form == 5 ? UINT64_C(0x7fff000000000005) : UINT64_C(0xfff7000000000005);
    for (unsigned shift = 0; shift < sizeof(bits) * kByteBits; shift += kByteBits)
      bytes.push_back(static_cast<std::uint8_t>(bits >> shift));
  }
  else bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), static_cast<std::uint8_t>(form == 1 ? 1 : 3)});
  if (form == 2) bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 4, op(abc::Opcode::kMUL2_IMM8_V8), 0, 0});
  if (form == 3) bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 0, op(abc::Opcode::kLDA_V8), 4});
  for (unsigned i = 0; i < count; ++i) {
    const auto encoding = static_cast<std::uint16_t>(unary);
    if (encoding > UINT8_MAX) bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 2,
        static_cast<std::uint8_t>(encoding), static_cast<std::uint8_t>(encoding >> kByteBits), 2});
    else bytes.insert(bytes.end(), {op(unary), 0});
  }
  if (form == 3) bytes.insert(bytes.end(), {op(abc::Opcode::kADD2_IMM8_V8), 0, 0});
  if (form == 4) bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDAI_IMM32), 2, 0, 0, 0, op(abc::Opcode::kEXP_IMM8_V8), 0, 0});
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
std::uint64_t Run(abc::ProfileId profile, abc::Opcode unary, unsigned form, unsigned count, bool phased, bool print) {
  const auto body = Body(profile, unary, form, count, phased);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(3, 3, {}));
  RouteInputIdentity identity; identity.profile = profile;
  identity.contract = SourceContract::kLanguageNumericV1;
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = {300}; syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"left", u"right", u"third"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}, {NameId{1}, NameEvidence::kDebugRange},
      {NameId{2}, NameEvidence::kDebugRange}};
  if (!phased && form == 0 && count == 1) {
    if (unary == kOperations[0] || unary == kOperations[1] || unary == kOperations[2] ||
        unary == kOperations[5] || unary == kOperations[6] || unary == kOperations[7]) {
      for (const auto contract : {kSourceContract, SourceContract::kLanguageArithmeticV1}) {
        auto pinned = identity; pinned.contract = contract;
        test::Check(std::holds_alternative<FunctionInputFailure>(FunctionInput::Build(pinned,
            {300}, {100}, body, frame, InputCaptureMode::kClosed)));
      }
    }
    const auto private_input = Body(profile, unary, form, count, phased, true);
    test::Check(std::holds_alternative<FunctionInputFailure>(FunctionInput::Build(identity,
        {300}, {100}, private_input, frame, InputCaptureMode::kClosed)));
  }
  std::ostringstream output;
  std::uint64_t work = 0;
  if (!phased) {
    const auto prepared = FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed);
    test::Check(std::holds_alternative<FunctionInput>(prepared));
    const auto& input = std::get<FunctionInput>(prepared);
    if (form == 0) {
      IdfQuota linear_work{kDefaultSemanticWork};
      test::Check(std::holds_alternative<LinearLexicals>(LinearLexicals::Build(body,
          input.Proof().environments, frame.registers, linear_work, {}, {}, CapturedValues::kReject,
          {}, {}, {}, Arithmetic(identity.contract))));
    }
    const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
    const auto& graph = values.graph;
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph)); work = evaluation.work;
    std::vector<ExpressionConstant> constants(graph.ops.size()); const ExpressionConstants resolved{constants, syntax.names.size()};
    const auto tree = std::get<ConstructedFunction>(BuildFunctionTree(graph, control, evaluation, syntax, resolved)).tree;
    test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
    test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(input, graph, tree)));
    auto changed_contract = tree; changed_contract.identity.contract = kSourceContract;
    test::Check(std::holds_alternative<InstructionSourceError>(BuildInstructionSources(input, graph, changed_contract)));
    if (unary == kOperations[5] && form == 0 && count == 1) {
      auto stale = tree; stale.identity.contract = SourceContract::kLanguageArithmeticV1;
      test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, stale, resolved)));
      auto internal = graph;
      for (auto& value : internal.values) if (std::holds_alternative<EntryDefinition>(value.origin)) value.domain = ValueKinds::Hole();
      test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(internal, control, evaluation, tree, resolved)));
    }
    if (form >= 5) {
      auto pinned_text = tree; pinned_text.identity.contract = kSourceContract;
      std::ostringstream refused;
      test::Check(std::holds_alternative<TreePrintError>(PrintFunctionTree(pinned_text, refused)));
      auto private_bits = tree;
      for (auto& node : private_bits.expressions) if (auto* literal = std::get_if<TreeLiteral>(&node.expression))
        if (auto* number = std::get_if<NumberLiteral>(literal)) number->bits = UINT64_C(0xffff000000000005);
      std::ostringstream private_text;
      test::Check(std::holds_alternative<TreePrintError>(PrintFunctionTree(private_bits, private_text)));
    }
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
        FrameReceiver::kUnknown, Arithmetic(identity.contract)));
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
    test::Check(std::holds_alternative<FunctionInputFailure>(FunctionInput::Build(identity,
        {300}, {100}, body, frame, InputCaptureMode::kClosed)));
    identity.contract = SourceContract::kLanguageNumericV1;
    const auto input = FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed);
    test::Check(std::holds_alternative<FunctionInput>(input));
    const auto& proof = std::get<FunctionInput>(input).Proof();
    test::Check(proof.arithmetic == ArithmeticSemantics::kSourceNumeric);
    IdfQuota linear_work{kDefaultSemanticWork};
    test::Check(std::holds_alternative<LinearLexicals>(LinearLexicals::Build(body,
        proof.environments, frame.registers, linear_work, {}, {}, CapturedValues::kReject,
        {}, {}, {}, Arithmetic(identity.contract))));
  }

  if (argc > 1) std::cout << "export default [\n";
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (unsigned op = 0; op < kOperations.size(); ++op) for (unsigned form = 0; form < 7; ++form)
      for (const bool phased : {false, true}) {
        if (argc > 1) std::cout << "{op:" << op << ",form:" << form << ",phased:" << (phased ? "true" : "false") << ",fn:";
        Run(profile, kOperations[op], form, 1, phased, argc > 1);
        if (argc > 1) std::cout << "},\n";
      }
  if (argc > 1) std::cout << "];\n";
  if (argc == 1) for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto operation : kOperations) for (const bool phased : {false, true}) {
    std::uint64_t previous = 0;
    for (const auto count : {200U, 201U, 402U}) {
      const auto work = Run(profile, operation, 0, count, phased, false);
      if (count == 402) test::Check(work < 3 * previous);
      previous = work;
    }
  }
}
