#include "../../src/source/function_tree.h"
#include "../../src/source/family_print.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/instruction_sources.h"
#include "../support/analysis/ssa_fixture.h"
#include "../support/instruction_positions.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include <array>
#include <nlohmann/json.hpp>
#include <sstream>

using namespace abcd;
using namespace abcd::source;
using test::Check;
namespace {
abc::MethodBody Body(unsigned depth, bool first_call, bool tail_call, bool computed, bool formed,
    unsigned copies, bool arguments, abc::ProfileId profile, bool repeated) {
  std::vector<std::uint8_t> bytes;
  const auto emit = [&](abc::Opcode opcode) {
    const auto wire = static_cast<std::uint16_t>(opcode); bytes.push_back(static_cast<std::uint8_t>(wire));
    if (wire > UINT8_MAX) bytes.push_back(static_cast<std::uint8_t>(wire >> 8));
  };
  const auto load = [&](std::uint8_t slot) { emit(abc::Opcode::kLDA_V8); bytes.push_back(slot); };
  const auto store = [&](std::uint8_t slot) { emit(abc::Opcode::kSTA_V8); bytes.push_back(slot); };
  const auto jump = [&](abc::Opcode opcode) {
    const auto at = bytes.size(); emit(opcode); bytes.insert(bytes.end(), sizeof(std::int32_t), 0); return at;
  };
  const auto patch = [&](std::size_t at, std::size_t target) {
    const auto delta = static_cast<std::uint32_t>(target - at);
    for (unsigned i = 0; i < sizeof(delta); ++i) bytes[at + 1 + i] = static_cast<std::uint8_t>(delta >> (i * 8));
  };
  const auto invoke = [&](bool member) {
    store(1);
    if (arguments) {load(4); emit(abc::Opcode::kCALLARG0_IMM8); bytes.push_back(0); store(2);}
    load(1);
    emit(member ? arguments ? abc::Opcode::kCALLTHIS1_IMM8_V8_V8 : abc::Opcode::kCALLTHIS0_IMM8_V8
      : arguments ? abc::Opcode::kCALLARG1_IMM8_V8 : abc::Opcode::kCALLARG0_IMM8);
    bytes.push_back(0); if (member) bytes.push_back(0); if (arguments) bytes.push_back(2);
    store(0);
  };
  if (formed) {load(5); emit(abc::Opcode::kCALLARG0_IMM8); bytes.push_back(0);} else load(3);
  store(0);
  for (unsigned i = 0; i < copies; ++i) {load(0); store(1); const auto split = jump(abc::Opcode::kJMP_IMM32); patch(split, bytes.size()); load(1); store(0);}
  std::array<std::size_t, 2> exits;
  for (unsigned i = 0; i < exits.size(); ++i) {
    emit(i == 0 ? abc::Opcode::kLDNULL : abc::Opcode::kLDUNDEFINED);
    emit(abc::Opcode::kSTRICTNOTEQ_IMM8_V8); bytes.insert(bytes.end(), {0, 0}); exits[i] = jump(abc::Opcode::kJEQZ_IMM32);
  }
  if (first_call) {load(0); invoke(false);}
  for (unsigned i = 0; i < depth; ++i) {
    if (computed) {load(4); emit(abc::Opcode::kLDOBJBYVALUE_IMM8_V8); bytes.insert(bytes.end(), {0, 0});}
    else {load(0); emit(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.insert(bytes.end(), {0, 0, 0});}
    if (i + 1 < depth || tail_call) {invoke(true); if (repeated) {load(0); invoke(false);}} else store(0);
  }
  load(0); const auto end = jump(abc::Opcode::kJMP_IMM32);
  for (const auto at : exits) patch(at, bytes.size()); emit(abc::Opcode::kLDUNDEFINED);
  patch(end, bytes.size()); emit(abc::Opcode::kRETURN);
  abc::MethodBody body;
  for (std::uint32_t pc = 0; pc < bytes.size();) {
    const auto decoded = std::get<abc::DecodedInstruction>(abc::DecodeInstruction(std::span(bytes).subspan(pc), profile));
    abc::BodyInstruction instruction{{pc}, decoded, {}};
    if (decoded.branch_delta) instruction.branch_target = abc::CodeOffset{static_cast<std::uint32_t>(pc + *decoded.branch_delta)};
    body.instructions.push_back(instruction); pc += decoded.size;
  }
  return body;
}
std::string Source(unsigned depth, bool first, bool tail, bool computed, bool formed, unsigned copies,
    bool arguments, abc::ProfileId profile, bool repeated = false) {
  const auto body = Body(depth, first, tail, computed, formed, copies, arguments, profile, repeated);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(3, 3, {}));
  RouteInputIdentity identity{}; identity.profile = profile;
  const auto input = FunctionInput::Build(identity, {}, {}, body, frame, InputCaptureMode::kClosed);
  Check(std::holds_alternative<FunctionInput>(input)); const auto& source = std::get<FunctionInput>(input);
  const auto imported = analysis::ImportNormalValues(body, source.Input(), source.Proof(), frame);
  Check(std::holds_alternative<analysis::ValueProgram>(imported)); auto graph = std::get<analysis::ValueProgram>(imported);
  Check(!std::holds_alternative<analysis::ValueSimplifyError>(analysis::SimplifyValues(graph, graph.revision)));
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto planned = BuildEvaluationPlan(graph); Check(std::holds_alternative<EvaluationPlan>(planned));
  const auto& evaluation = std::get<EvaluationPlan>(planned);
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"value", u"key", u"source", u"field"};
  for (std::uint32_t i = 0; i < 3; ++i) syntax.parameters.push_back({NameId{i}, NameEvidence::kDebugRange});
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) if (std::holds_alternative<analysis::ValueNamedRead>(graph.ops[i].operation)) constants[i] = NameId{3};
  const ExpressionConstants view{constants, 4, 0, {}, {}};
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), view);
  Check(std::holds_alternative<ConstructedFunction>(built)); const auto& tree = std::get<ConstructedFunction>(built).tree;
  Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, view)));
  const auto locations = BuildInstructionSources(source, graph, tree); Check(std::holds_alternative<InstructionSources>(locations));
  const auto family = std::get<ConstructedFamily>(BuildFunctionFamily({{tree, {}, {}}}, tree.owner));
  std::ostringstream text; const auto printed = PrintFamilyDraft(family, text); Check(std::holds_alternative<PrintedFamilyDraft>(printed));
  test::CheckInstructionPositions(tree, std::get<InstructionSources>(locations),
    std::get<PrintedFamilyDraft>(printed).members.front(), std::get<PrintedFamilyDraft>(printed).functions.front());
  return text.str();
}
}
int main(int argc, char**) {
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const unsigned depth : {1u, 3u, 200u, 201u}) for (const bool first : {false, true})
      for (const bool tail : {false, true}) for (const bool computed : {false, true})
        for (const bool formed : {false, true}) for (const unsigned copies : {0u, 201u}) for (const bool arguments : {false, true}) {
          const auto text = Source(depth, first, tail, computed, formed, copies, arguments, profile);
          Check(text.find("?.") != std::string::npos);
          if (argc > 1) std::cout << nlohmann::json{{"depth", depth}, {"firstCall", first}, {"tailCall", tail},
            {"computed", computed}, {"formed", formed}, {"arguments", arguments}, {"copies", copies},
            {"profile", unsigned(profile)}, {"source", text}} << '\n';
        }
  // Actual CALLARG consumers of CALLTHIS results share the same source route.
  // Repeat copies/block splits independently of callee and argument effects.
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const unsigned depth : {1u, 3u}) for (const bool first : {false, true})
      for (const bool computed : {false, true}) for (const bool formed : {false, true})
        for (const unsigned copies : {0u, 201u}) for (const bool arguments : {false, true}) {
          const auto text = Source(depth, first, true, computed, formed, copies, arguments, profile, true);
          Check(text.find("?.") != std::string::npos);
          if (argc > 1) std::cout << nlohmann::json{{"depth", depth}, {"firstCall", first}, {"tailCall", true},
            {"computed", computed}, {"formed", formed}, {"arguments", arguments}, {"copies", copies},
            {"repeated", true}, {"profile", unsigned(profile)}, {"source", text}} << '\n';
        }
}
