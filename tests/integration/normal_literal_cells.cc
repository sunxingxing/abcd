#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/instruction_sources.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
enum class Form { kDirect, kCopies, kSplit, kNested };
abc::MethodBody Body(abc::ProfileId profile, unsigned count, Form form) {
  std::vector<std::uint8_t> bytes;
  const auto emit = [&](abc::Opcode opcode) {
    const auto wire = static_cast<std::uint16_t>(opcode);
    bytes.push_back(static_cast<std::uint8_t>(wire));
    if (wire > UINT8_MAX) bytes.push_back(static_cast<std::uint8_t>(wire >> 8));
  };
  const auto load = [&](std::uint8_t slot) { emit(abc::Opcode::kLDA_V8); bytes.push_back(slot); };
  const auto store = [&](std::uint8_t slot) { emit(abc::Opcode::kSTA_V8); bytes.push_back(slot); };
  const auto define = [&](unsigned name, std::uint8_t slot) {
    emit(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8);
    bytes.insert(bytes.end(), {0, static_cast<std::uint8_t>(name), static_cast<std::uint8_t>(name >> 8), slot});
  };
  emit(abc::Opcode::kNEWLEXENV_IMM8); bytes.push_back(1);
  load(3); emit(abc::Opcode::kSTLEXVAR_IMM4_IMM4); bytes.push_back(0);
  if (form == Form::kSplit) {
    emit(abc::Opcode::kJMP_IMM32);
    bytes.insert(bytes.end(), {5, 0, 0, 0});
  }
  emit(abc::Opcode::kCREATEEMPTYOBJECT); store(0);
  for (unsigned i = 0; i < count; ++i) {
    if (form == Form::kNested) { emit(abc::Opcode::kCREATEEMPTYOBJECT); store(1); }
    emit(abc::Opcode::kLDLEXVAR_IMM4_IMM4); bytes.push_back(0);
    if (form == Form::kCopies) { store(2); load(2); }
    emit(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16); bytes.insert(bytes.end(), {0, 0});
    emit(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.insert(bytes.end(), {0, 0, 0});
    if (form == Form::kNested) { define(count, 1); load(1); }
    define(i, 0);
  }
  load(0); emit(abc::Opcode::kRETURN);
  abc::MethodBody body;
  for (std::uint32_t pc = 0; pc < bytes.size();) {
    const auto decoded = std::get<abc::DecodedInstruction>(abc::DecodeInstruction(std::span(bytes).subspan(pc), profile));
    abc::BodyInstruction instruction{{pc}, decoded, {}};
    if (decoded.branch_delta) instruction.branch_target = abc::CodeOffset{static_cast<std::uint32_t>(pc + *decoded.branch_delta)};
    body.instructions.push_back(instruction); pc += decoded.size;
  }
  return body;
}
std::uint64_t Run(abc::ProfileId profile, unsigned count, Form form, bool print) {
  const auto body = Body(profile, count, form);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(3, 1, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto prepared = FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed);
  test::Check(std::holds_alternative<FunctionInput>(prepared));
  const auto& input = std::get<FunctionInput>(prepared);
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto planned = BuildEvaluationPlan(graph);
  test::Check(std::holds_alternative<EvaluationPlan>(planned));
  const auto& evaluation = std::get<EvaluationPlan>(planned);
  test::Check(!std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, evaluation)));
  if (count == 2) {
    test::Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(graph)));
    test::Check(std::holds_alternative<LiteralPlan>(BuildLiteralPlan(graph, {}, {}, {}, {}, &*evaluation.cells)));
    test::Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(graph, *evaluation.literals)));
    auto invalid = *evaluation.cells;
    ++invalid.initialization.revision.value;
    test::Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(graph, {}, {}, {}, {}, &invalid)));
    test::Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(graph, *evaluation.literals, {}, {}, {}, {}, &invalid)));
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      if (!std::holds_alternative<analysis::ValueLexicalLoad>(graph.ops[i].operation)) continue;
      invalid = *evaluation.cells;
      invalid.initialization.reads[i] = NotCellRead{};
      test::Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(graph, {}, {}, {}, {}, &invalid)));
      test::Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(graph, *evaluation.literals, {}, {}, {}, {}, &invalid)));
      auto uninitialized = graph;
      uninitialized.values[graph.ops[i].result->value].domain = analysis::ValueKinds::Hole();
      test::Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(uninitialized, {}, {}, {}, {}, &*evaluation.cells)));
    }
  }
  FunctionSyntax syntax; syntax.identity = identity; syntax.method = {300};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  for (unsigned i = 0; i < count; ++i) {
    const auto name = std::string("field") + std::to_string(i); syntax.names.emplace_back(name.begin(), name.end());
  }
  syntax.names.push_back(u"probe"); syntax.names.push_back(u"object");
  syntax.parameters = {{NameId{count + 1}, NameEvidence::kDebugRange}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (const auto* field = std::get_if<analysis::ValueOwnWrite>(&graph.ops[i].operation)) constants[i] = NameId{field->name.value};
    else if (std::holds_alternative<analysis::ValueNamedRead>(graph.ops[i].operation)) constants[i] = NameId{count};
  const ExpressionConstants view{constants, syntax.names.size()};
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), view);
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  unsigned objects = 0;
  for (const auto& expression : tree.expressions)
    objects += std::holds_alternative<ObjectExpr>(expression.expression);
  test::Check(objects == (form == Form::kNested ? count + 1 : 1));
  const auto semantics = VerifyTreeSemantics(graph, control, evaluation, tree, view);
  test::Check(std::holds_alternative<TreeSemantics>(semantics));
  test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(input, graph, tree)));
  std::ostringstream output;
  test::Check(!std::get<PrintedFunction>(PrintFunctionTree(tree, output)).incomplete);
  if (print) std::cout << output.str();
  return evaluation.work + std::get<ConstructedFunction>(built).work + std::get<TreeSemantics>(semantics).work;
}
}
int main(int argc, char**) {
  const bool print = argc > 1;
  if (print) std::cout << "export default [\n";
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto form : {Form::kDirect, Form::kCopies, Form::kSplit, Form::kNested}) {
      std::uint64_t previous = 0;
      for (const auto count : {2U, 200U, 201U, 402U}) {
        const auto work = Run(profile, count, form, print);
        if (count == 402) test::Check(work < previous * 3);
        previous = work;
        if (print) std::cout << ",\n";
      }
    }
  if (print) std::cout << "];\n";
}
