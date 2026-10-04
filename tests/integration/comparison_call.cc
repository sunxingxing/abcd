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
enum class Form { kDirect, kCopies, kSplit, kInverted, kCells };
constexpr std::array kComparisons{
  abc::Opcode::kEQ_IMM8_V8, abc::Opcode::kNOTEQ_IMM8_V8,
  abc::Opcode::kLESS_IMM8_V8, abc::Opcode::kLESSEQ_IMM8_V8,
  abc::Opcode::kGREATER_IMM8_V8, abc::Opcode::kGREATEREQ_IMM8_V8,
  abc::Opcode::kISIN_IMM8_V8, abc::Opcode::kINSTANCEOF_IMM8_V8,
  abc::Opcode::kSTRICTEQ_IMM8_V8, abc::Opcode::kSTRICTNOTEQ_IMM8_V8};
abc::MethodBody Body(abc::ProfileId profile, abc::Opcode comparison, Form form, unsigned count) {
  const auto byte = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  std::vector<std::uint8_t> bytes;
  const auto load = [&](std::uint8_t reg) { bytes.insert(bytes.end(), {byte(abc::Opcode::kLDA_V8), reg}); };
  const auto store = [&](std::uint8_t reg) { bytes.insert(bytes.end(), {byte(abc::Opcode::kSTA_V8), reg}); };
  const auto read = [&](std::uint8_t reg, std::uint8_t name) {
    load(reg); bytes.insert(bytes.end(), {byte(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, name, 0});
  };
  if (form == Form::kCells) {
    bytes.insert(bytes.end(), {byte(abc::Opcode::kNEWLEXENV_IMM8), 1});
    load(5); bytes.insert(bytes.end(), {byte(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0});
  }
  for (unsigned i = 0; i < count; ++i) {
    read(4, 0); store(0);
    if (form == Form::kCopies) { load(0); store(3); load(3); store(0); }
    if (form == Form::kSplit)
      for (unsigned prefix = 0; prefix <= i % 4; ++prefix)
        bytes.insert(bytes.end(), {byte(abc::Opcode::kJMP_IMM8), 2});
    if (form == Form::kCells) {
      bytes.insert(bytes.end(), {byte(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0});
      const auto guard = static_cast<std::uint16_t>(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16);
      bytes.insert(bytes.end(), {static_cast<std::uint8_t>(guard), static_cast<std::uint8_t>(guard >> 8), 0, 0});
      store(3);
    }
    const std::uint8_t subject = form == Form::kCells ? 3 : 5;
    read(subject, 1); store(1); read(subject, 2);
    bytes.insert(bytes.end(), {byte(comparison), 0, 1});
    if (form == Form::kInverted) bytes.push_back(byte(abc::Opcode::kISFALSE));
    const auto branch = bytes.size(); bytes.insert(bytes.end(), {byte(abc::Opcode::kJEQZ_IMM8), 0});
    bytes.insert(bytes.end(), {byte(abc::Opcode::kLDAI_IMM32), 7, 0, 0, 0});
    const auto jump = bytes.size(); bytes.insert(bytes.end(), {byte(abc::Opcode::kJMP_IMM8), 0});
    bytes[branch + 1] = static_cast<std::uint8_t>(bytes.size() - branch);
    read(subject, 3);
    bytes[jump + 1] = static_cast<std::uint8_t>(bytes.size() - jump);
    store(2); load(0);
    bytes.insert(bytes.end(), {byte(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, 4, 2});
  }
  bytes.push_back(byte(abc::Opcode::kRETURN));
  abc::MethodBody body;
  for (std::uint32_t pc = 0; pc < bytes.size();) {
    const auto decoded = std::get<abc::DecodedInstruction>(abc::DecodeInstruction(std::span(bytes).subspan(pc), profile));
    abc::BodyInstruction instruction{{pc}, decoded, {}};
    if (decoded.branch_delta) instruction.branch_target = abc::CodeOffset{static_cast<std::uint32_t>(pc + *decoded.branch_delta)};
    body.instructions.push_back(instruction); pc += decoded.size;
  }
  return body;
}
std::uint64_t Run(abc::ProfileId profile, abc::Opcode comparison, Form form, unsigned count, bool print) {
  const auto body = Body(profile, comparison, form, count);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(4, 2, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto prepared = FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed);
  test::Check(std::holds_alternative<FunctionInput>(prepared)); const auto& input = std::get<FunctionInput>(prepared);
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  test::Check(!std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, evaluation)));
  if (count == 2) {
    unsigned snapshots = 0;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      const auto& placement = evaluation.operations[i];
      if (placement.kind == EvaluationKind::kPredicateSnapshot) {
        ++snapshots;
        auto invalid = evaluation;
        invalid.operations[i].consumer.reset();
        test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, invalid)));
      }
      if (form == Form::kCells && std::holds_alternative<analysis::ValueLexicalLoad>(graph.ops[i].operation)) {
        test::Check(placement.kind == EvaluationKind::kPredicateSnapshot && evaluation.cells);
        auto invalid = evaluation;
        invalid.cells->initialization.reads[i] = NotCellRead{};
        test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, invalid)));
      }
      if (!std::holds_alternative<analysis::ValueNamedRead>(graph.ops[i].operation) ||
          std::get<analysis::ValueNamedRead>(graph.ops[i].operation).name.value != 0) continue;
      test::Check(placement.kind == EvaluationKind::kInline && placement.consumer);
      auto invalid = evaluation;
      invalid.operations[i].consumer = analysis::UseId{graph.ops[i].first_use.value};
      test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, invalid)));
    }
    test::Check(snapshots == (form == Form::kCells ? count : 0));
  }
  FunctionSyntax syntax; syntax.identity = identity; syntax.method = {300};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"method", u"left", u"right", u"alternate", u"target", u"subject"};
  syntax.parameters = {{NameId{4}, NameEvidence::kDebugRange}, {NameId{5}, NameEvidence::kDebugRange}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (const auto* field = std::get_if<analysis::ValueNamedRead>(&graph.ops[i].operation)) constants[i] = NameId{field->name.value};
  const ExpressionConstants view{constants, syntax.names.size()};
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), view);
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  const auto semantics = VerifyTreeSemantics(graph, control, evaluation, tree, view);
  test::Check(std::holds_alternative<TreeSemantics>(semantics));
  test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(input, graph, tree)));
  unsigned conditionals = 0;
  for (const auto& expression : tree.expressions) conditionals += std::holds_alternative<ConditionalExpr>(expression.expression);
  test::Check(conditionals == count);
  std::ostringstream output; test::Check(!std::get<PrintedFunction>(PrintFunctionTree(tree, output)).incomplete);
  if (print) std::cout << output.str();
  return evaluation.work + std::get<ConstructedFunction>(built).work + std::get<TreeSemantics>(semantics).work;
}
}
int main(int argc, char**) {
  const bool print = argc > 1;
  if (print) std::cout << "export default [\n";
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto comparison : kComparisons)
      for (const auto form : {Form::kDirect, Form::kCopies, Form::kSplit, Form::kInverted, Form::kCells}) {
        Run(profile, comparison, form, 2, print);
        if (print) std::cout << ",\n";
      }
  if (print) std::cout << "];\n";
  else {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto form : {Form::kSplit, Form::kCells}) {
        std::uint64_t previous = 0;
        for (const auto count : {200U, 201U, 402U}) {
          const auto work = Run(profile, abc::Opcode::kLESS_IMM8_V8, form, count, false);
          if (count == 402) test::Check(work < previous * 3);
          previous = work;
        }
      }
  }
}
