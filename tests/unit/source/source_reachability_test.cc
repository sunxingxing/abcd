#include "../../../src/source/function_tree.h"
#include "../../../src/source/instruction_sources.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/family_print.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include "analysis/ssa_fixture.h"
#include "instruction_positions.h"

#include <sstream>

using namespace abcd;
using namespace abcd::source;
using abcd::test::Check;
namespace {
constexpr auto Wire(abc::Opcode opcode) { return static_cast<std::uint8_t>(opcode); }
void Verify(abc::ProfileId profile, bool diamond, unsigned count) {
  std::vector<std::uint8_t> bytes;
  if (diamond) {
    bytes = {Wire(abc::Opcode::kLDA_V8), 0, Wire(abc::Opcode::kJEQZ_IMM8), 12,
        Wire(abc::Opcode::kLDAI_IMM32), 11, 0, 0, 0, Wire(abc::Opcode::kJMP_IMM8), 10,
        Wire(abc::Opcode::kLDUNDEFINED), Wire(abc::Opcode::kJMP_IMM8), 7,
        Wire(abc::Opcode::kLDAI_IMM32), 22, 0, 0, 0, Wire(abc::Opcode::kRETURN)};
  } else {
    bytes = {Wire(abc::Opcode::kLDAI_IMM32), 42, 0, 0, 0, Wire(abc::Opcode::kRETURN)};
    for (unsigned i = 0; i < count; ++i) bytes.push_back(Wire(abc::Opcode::kLDUNDEFINED));
    bytes.push_back(Wire(abc::Opcode::kRETURNUNDEFINED));
  }
  const auto body = test::Body(bytes);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {false, false, false, false}));
  RouteInputIdentity identity{}; identity.profile = profile;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {7}, {9}, body, frame, InputCaptureMode::kClosed));
  auto graph = std::get<analysis::ValueProgram>(analysis::ImportNormalValues(body, input.Input(), input.Proof(), frame));
  Check(!analysis::VerifyImportedValues(graph, input.Input(), input.Proof(), frame));
  Check(std::holds_alternative<analysis::ValueSimplifyStats>(analysis::SimplifyValues(graph, graph.revision)));
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = {7};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"value"}; syntax.parameters.push_back({NameId{0}, NameEvidence::kDebugRange});
  std::vector<ExpressionConstant> constants(graph.ops.size());
  const ExpressionConstants view{constants, 1, 0, {}};
  const auto tree = std::get<ConstructedFunction>(BuildFunctionTree(graph, control, evaluation, std::move(syntax), view)).tree;
  Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, view)));
  const auto sources = std::get<InstructionSources>(BuildInstructionSources(input, graph, tree));
  Check(sources.input_sites == body.instructions.size() && sources.occurrences.size() == body.instructions.size());
  unsigned unreachable = 0;
  for (const auto& source : sources.occurrences) {
    const bool dead = !input.Proof().environments.before[source.site.value];
    Check(std::holds_alternative<UnreachableInstruction>(source.target) == dead);
    if (!dead) continue;
    ++unreachable;
    const auto& origin = std::get<OriginalSite>(tree.origins[source.origin.value]);
    Check(origin.pc == body.instructions[source.site.value].offset);
  }
  Check(unreachable == (diamond ? 2 : count + 1));
  const auto dead = diamond ? 4U : 2U;
  auto wrong_tree = tree;
  ++std::get<OriginalSite>(wrong_tree.origins[dead]).pc.value;
  Check(std::get<InstructionSourceError>(BuildInstructionSources(input, graph, wrong_tree)) == InstructionSourceError::kOrigin);
  wrong_tree = tree; wrong_tree.operations.push_back({{dead}, tree.operations.front().node});
  Check(std::get<InstructionSourceError>(BuildInstructionSources(input, graph, wrong_tree)) == InstructionSourceError::kPlacement);
  auto wrong_graph = graph; wrong_graph.origins[dead].disposition = analysis::OriginDisposition::kElidedPure;
  Check(std::get<InstructionSourceError>(BuildInstructionSources(input, wrong_graph, tree)) == InstructionSourceError::kPlacement);
  wrong_tree = tree; wrong_tree.operations.erase(wrong_tree.operations.begin());
  Check(std::get<InstructionSourceError>(BuildInstructionSources(input, graph, wrong_tree)) == InstructionSourceError::kCoverage);
  auto family = std::get<ConstructedFamily>(BuildFunctionFamily({{tree, {}, {}}}, tree.owner));
  std::ostringstream output;
  const auto printed = std::get<PrintedFamilyDraft>(PrintFamilyDraft(family, output));
  test::CheckInstructionPositions(tree, sources, printed.members.front(), printed.functions.front());
  Check(!output.str().empty() && output.str().find("INCOMPLETE") == std::string::npos);
}
}
int main() {
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    Verify(profile, true, 0);
    for (const auto count : {1U, 200U, 201U, 400U}) Verify(profile, false, count);
  }
}
