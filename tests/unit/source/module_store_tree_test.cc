#include "../../../src/source/function_values.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"

#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
void Run(std::uint32_t count, abc::ProfileId profile, bool print) {
  const auto opcode = [](abc::Opcode op) { return static_cast<std::uint8_t>(op); };
  std::vector<std::uint8_t> bytes;
  for (std::uint32_t i = 0; i < count; ++i) {
    bytes.push_back(opcode(abc::Opcode::kLDAI_IMM32));
    for (unsigned b = 0; b < sizeof(i); ++b) bytes.push_back(static_cast<std::uint8_t>(i >> (b * 8)));
    bytes.insert(bytes.end(), {opcode(abc::Opcode::kSTMODULEVAR_IMM8), 0});
  }
  bytes.push_back(opcode(abc::Opcode::kRETURNUNDEFINED));
  const auto body = test::Body(bytes);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 0, {false, false, false, false}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  FunctionSyntax syntax{};
  syntax.identity = identity; syntax.method = {300}; syntax.owner = {1};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"exported"};
  syntax.modules.push_back({{{7}, {0}}, {0}, ModuleAccess::kMutable, NameEvidence::kMetadata, true});
  std::vector<ExpressionConstant> operations(graph.ops.size());
  std::vector<OpId> stores;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (const auto* store = std::get_if<ValueModuleWrite>(&graph.ops[i].operation)) {
      operations[i] = ModuleStoreConstant{store->slot, {{7}, {0}}, stores.empty()};
      stores.push_back({static_cast<std::uint32_t>(i)});
    }
  }
  const ExpressionConstants constants{operations, syntax.names.size(), 0};
  const auto built = BuildFunctionTree(graph, control, evaluation, syntax, constants);
  if (const auto* error = std::get_if<FunctionTreeError>(&built)) {
    std::cerr << "tree " << static_cast<unsigned>(*error) << '\n'; test::Check(false);
  }
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  const auto verified = VerifyTreeSemantics(graph, control, evaluation, tree, constants);
  test::Check(std::holds_alternative<TreeSemantics>(verified));
  std::ostringstream output;
  const auto printed = PrintModuleTree(tree, output);
  test::Check(std::holds_alternative<PrintedFunction>(printed));
  test::Check(output.str().find("let exported = 0;") != std::string::npos);
  test::Check(output.str().find("return") == std::string::npos);
  if (print) std::cout << output.str() << "export { exported };\n";
  auto wrong = operations;
  std::get<ModuleStoreConstant>(wrong[stores.front().value]).slot.value = 1;
  auto wrong_constants = constants; wrong_constants.operations = wrong;
  test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, tree, wrong_constants)));
  auto forged = tree;
  forged.modules[0].local = false;
  test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, forged, constants)));
  forged = tree; ++forged.modules[0].target.symbol.value;
  test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, forged, constants)));
  wrong = operations;
  std::get<ModuleStoreConstant>(wrong[stores.front().value]).initialize = false;
  wrong_constants.operations = wrong;
  test::Check(std::holds_alternative<FunctionTreeError>(BuildFunctionTree(graph, control, evaluation, syntax, wrong_constants)));
  auto readonly = syntax;
  readonly.modules[0].access = ModuleAccess::kReadOnly;
  test::Check(std::holds_alternative<FunctionTreeError>(BuildFunctionTree(graph, control, evaluation, readonly, constants)));
  if (count > 1) {
    wrong = operations;
    std::get<ModuleStoreConstant>(wrong[stores[1].value]).initialize = true;
    wrong_constants.operations = wrong;
    test::Check(std::holds_alternative<FunctionTreeError>(BuildFunctionTree(graph, control, evaluation, syntax, wrong_constants)));
  }
}
}
int main(int argc, char**) {
  if (argc > 1) { Run(3, abcd::abc::ProfileId::kDynamic1206, true); return 0; }
  for (const auto profile : {abcd::abc::ProfileId::kDynamic1202, abcd::abc::ProfileId::kDynamic1206,
                            abcd::abc::ProfileId::kDynamic1301})
    for (const auto count : {1U, 200U, 201U, 402U}) Run(count, profile, false);
}
