#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/instruction_sources.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
enum class Form { kDirect, kCopies, kSplit, kReversed, kScoped };
abc::MethodBody Body(abc::ProfileId profile, unsigned count, Form form) {
  std::vector<std::uint8_t> bytes;
  const auto emit = [&](abc::Opcode opcode) {
    const auto wire = static_cast<std::uint16_t>(opcode);
    bytes.push_back(static_cast<std::uint8_t>(wire));
    if (wire > UINT8_MAX) bytes.push_back(static_cast<std::uint8_t>(wire >> 8));
  };
  const auto load = [&](std::uint8_t slot) { emit(abc::Opcode::kLDA_V8); bytes.push_back(slot); };
  const auto store = [&](std::uint8_t slot) { emit(abc::Opcode::kSTA_V8); bytes.push_back(slot); };
  const auto jump = [&](abc::Opcode opcode) {
    const auto pc = bytes.size(); emit(opcode); const auto operand = bytes.size();
    bytes.insert(bytes.end(), sizeof(std::int32_t), 0); return std::pair{pc, operand};
  };
  const auto patch = [&](auto branch, std::size_t target) {
    const auto delta = static_cast<std::uint32_t>(target - branch.first);
    for (unsigned i = 0; i < sizeof(delta); ++i) bytes[branch.second + i] = static_cast<std::uint8_t>(delta >> (i * 8));
  };
  emit(abc::Opcode::kNEWLEXENV_IMM8); bytes.push_back(1);
  load(2); emit(abc::Opcode::kSTLEXVAR_IMM4_IMM4); bytes.push_back(0);
  emit(abc::Opcode::kLDTRUE); store(1);
  std::vector<std::pair<std::size_t, std::size_t>> exits;
  for (unsigned i = 0; i < count; ++i) {
    if (form == Form::kScoped && i != 0) {
      emit(abc::Opcode::kNEWLEXENV_IMM8); bytes.push_back(1);
      load(2); emit(abc::Opcode::kSTLEXVAR_IMM4_IMM4); bytes.push_back(0);
    }
    emit(abc::Opcode::kLDLEXVAR_IMM4_IMM4); bytes.push_back(0);
    if (form == Form::kCopies || form == Form::kSplit) { store(0); load(0); }
    if (form == Form::kSplit) { const auto split = jump(abc::Opcode::kJMP_IMM32); patch(split, bytes.size()); }
    emit(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16); bytes.insert(bytes.end(), {0, 0});
    emit(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.insert(bytes.end(), {0, 0, 0});
    if (form == Form::kScoped && i != 0) emit(abc::Opcode::kPOPLEXENV);
    if (form == Form::kReversed) { store(0); load(1); }
    emit(abc::Opcode::kSTRICTEQ_IMM8_V8); bytes.insert(bytes.end(), {0, static_cast<std::uint8_t>(form == Form::kReversed ? 0 : 1)});
    exits.push_back(jump(abc::Opcode::kJNEZ_IMM32));
  }
  load(4); emit(abc::Opcode::kRETURN);
  for (const auto exit : exits) patch(exit, bytes.size());
  load(3); emit(abc::Opcode::kRETURN);
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
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(2, 3, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto prepared = FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed);
  test::Check(std::holds_alternative<FunctionInput>(prepared)); const auto& input = std::get<FunctionInput>(prepared);
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  test::Check(!std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, evaluation)));
  FunctionSyntax syntax; syntax.identity = identity; syntax.method = {300};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"object", u"selected", u"fallback", u"probe"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}, {NameId{1}, NameEvidence::kDebugRange}, {NameId{2}, NameEvidence::kDebugRange}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (std::holds_alternative<analysis::ValueNamedRead>(graph.ops[i].operation)) constants[i] = NameId{3};
  const ExpressionConstants view{constants, syntax.names.size()};
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), view);
  test::Check(std::holds_alternative<ConstructedFunction>(built)); const auto& tree = std::get<ConstructedFunction>(built).tree;
  const auto semantics = VerifyTreeSemantics(graph, control, evaluation, tree, view);
  test::Check(std::holds_alternative<TreeSemantics>(semantics));
  test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(input, graph, tree)));
  unsigned guards = 0;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (evaluation.operations[i].kind != EvaluationKind::kCellGuard) continue;
    ++guards;
    test::Check(IsInitializedCellGuard(graph, *evaluation.cells, analysis::OpId{static_cast<std::uint32_t>(i)}));
    if (count != 2) continue;
    auto false_fact = evaluation;
    const auto use = graph.uses[graph.ops[i].first_use.value];
    const auto load = std::get<analysis::OpResult>(graph.values[std::get<analysis::ValueId>(use.operand).value].origin).op;
    false_fact.cells->initialization.reads[load.value] = NotCellRead{};
    test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, false_fact)));
    auto changed = tree;
    const auto placement = std::find_if(changed.operations.begin(), changed.operations.end(), [&](const auto& p) { return p.operation.value == i; });
    test::Check(placement != changed.operations.end());
    const auto anchor = std::get<AnchorId>(placement->node);
    ++std::get<BlockBoundary>(changed.anchors[anchor.value].position).index;
    test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, changed, view)));
    changed = tree; changed.anchors[anchor.value].origin = changed.statements[changed.body.value].origin;
    test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, changed, view)));
  }
  test::Check(guards == count);
  std::ostringstream output; test::Check(!std::get<PrintedFunction>(PrintFunctionTree(tree, output)).incomplete);
  if (print) std::cout << output.str();
  return std::get<ConstructedFunction>(built).work + std::get<TreeSemantics>(semantics).work;
}
}
int main(int argc, char**) {
  const bool print = argc > 1;
  if (print) std::cout << "export default [\n";
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto form : {Form::kDirect, Form::kCopies, Form::kSplit, Form::kReversed, Form::kScoped}) {
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
