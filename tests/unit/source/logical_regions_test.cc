#include "../../../src/source/logical_regions.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/family_print.h"
#include <sstream>
#include "../../support/analysis/ssa_fixture.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"

using namespace abcd;
using namespace abcd::source;
using abcd::test::Check;
namespace {
analysis::ValueProgram Diamond(bool negate, unsigned count = 1, bool direct = false, bool getter = false, bool barrier = false, bool invoke = false) {
  // return arg || [] (or arg && []), retaining the compiler's empty keep arm.
  std::vector<std::uint8_t> bytes;
  for (unsigned i = 0; i < count; ++i) {
    std::vector<std::uint8_t> diamond{0x60, 2, 0x61, 1, 0xfb, 0x13, 0,
        static_cast<std::uint8_t>(negate ? 0x4f : 0x51), 0};
    if (getter) {
      diamond.insert(diamond.end(), {0x60, 2, 0x42, 0, 0, 0});
      if (invoke) diamond.insert(diamond.end(), {0x2d, 0, 2});
    }
    else diamond.insert(diamond.end(), {0x05, 0});
    if (barrier) diamond.push_back(static_cast<std::uint8_t>(abc::Opcode::kDEBUGGER));
    diamond.insert(diamond.end(), {static_cast<std::uint8_t>(direct ? 0x61 : 0x4d),
        static_cast<std::uint8_t>(direct ? 1 : 4)});
    // Branch offsets target the preserved-value load, or the shared reload.
    constexpr std::size_t branch_pc = 7, delta_operand = 8;
    diamond[delta_operand] = static_cast<std::uint8_t>(diamond.size() - branch_pc);
    diamond.insert(diamond.end(), {0x60, 1, 0x61, 2});
    bytes.insert(bytes.end(), diamond.begin(), diamond.end());
  }
  bytes.insert(bytes.end(), {0x60, 2, 0x64});
  const auto body = test::Body(bytes);
  using namespace analysis;
  const auto method_result = BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, 3);
  Check(std::holds_alternative<SemanticMethod>(method_result));
  const auto& method = std::get<SemanticMethod>(method_result);
  const auto proof_result = VerifySemanticSource(body, method, 2, 1);
  Check(std::holds_alternative<SemanticProof>(proof_result));
  const auto& proof = std::get<SemanticProof>(proof_result);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(2, 1, {false, false, false, false}));
  auto imported = ImportNormalValues(body, method, proof, frame);
  Check(std::holds_alternative<ValueProgram>(imported));
  auto graph = std::get<ValueProgram>(std::move(imported));
  Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph, graph.revision)));
  return graph;
}
}
std::string Source(const analysis::ValueProgram& graph) {
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  const auto regions = FindLogicalRegions(graph, control);
  for (const auto& region : regions) {
    const auto value = std::get<analysis::ValueId>(region.right);
    const auto producer = std::get<analysis::OpResult>(graph.values[value.value].origin).op;
    Check(evaluation.operations[producer.value].kind == EvaluationKind::kPhiInput);
    Check(!evaluation.operations[producer.value].temporary);
  }
  FunctionSyntax syntax{};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"value", u"probe"}; syntax.parameters.push_back({NameId{0}, NameEvidence::kDebugRange});
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (std::holds_alternative<analysis::ValueNamedRead>(graph.ops[i].operation)) constants[i] = NameId{1};
  const ExpressionConstants view{constants, 2, 0, {}};
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), view);
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, view)));
  if (!regions.empty()) {
    auto wrong = tree;
    const auto value = std::get<analysis::ValueId>(regions.front().right);
    const auto producer = std::get<analysis::OpResult>(graph.values[value.value].origin).op;
    for (const auto& placement : wrong.operations) if (placement.operation == producer) {
      const auto expression = std::get<ExprId>(placement.node);
      wrong.expressions[expression.value].expression = TreeLiteral{UndefinedLiteral{}};
    }
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong, view)));
  }
  auto family = std::get<ConstructedFamily>(BuildFunctionFamily({{tree, {}, {}}}, tree.owner));
  std::ostringstream output;
  Check(std::holds_alternative<PrintedFamilyDraft>(PrintFamilyDraft(family, output)));
  return output.str();
}
void PhiInputs() {
  using namespace analysis;
  auto graph = Diamond(false, 1, true);
  auto plan = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto region = FindLogicalRegions(graph, control).front();
  const auto value = std::get<ValueId>(region.right);
  const auto producer = std::get<OpResult>(graph.values[value.value].origin).op;
  auto forged = plan; ++forged.operations[producer.value].consumer->value;
  Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, forged)));
  // Decode a real intervening debugger event instead of forging graph state.
  graph = Diamond(false, 1, true, false, true);
  plan = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  const auto with_barrier = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto selected = FindLogicalRegions(graph, with_barrier).front();
  const auto rhs = std::get<ValueId>(selected.right);
  const auto allocation = std::get<OpResult>(graph.values[rhs.value].origin).op;
  Check(plan.operations[allocation.value].kind == EvaluationKind::kRoot);
  forged = plan;
  forged.operations[allocation.value] = {EvaluationKind::kPhiInput, graph.values[rhs.value].first_use, {}};
  Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, forged)));
}
void Negatives() {
  using namespace analysis;
  auto graph = Diamond(false);
  auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto region = FindLogicalRegions(graph, control).front();
  auto wrong = graph;
  std::get<ValueUnary>(wrong.ops[region.boolean.value].operation).op = UnaryOperator::kTypeOf;
  Check(FindLogicalRegions(wrong, control).empty());
  wrong = graph;
  // A nonempty keep arm must not be discarded just because its phi matches.
  Check(region.keep.has_value());
  auto& keep = wrong.ops[wrong.blocks[region.keep->value].first_op.value];
  keep.operation = ValueDebugger{}; keep.result.reset(); keep.use_count = 0; keep.life = ValueLife::kLive;
  Check(FindLogicalRegions(wrong, control).empty());
  wrong = graph;
  // An available but different value is a conditional, not logical selection.
  const ValueId other{static_cast<std::uint32_t>(wrong.values.size())};
  wrong.values.push_back({EntryDefinition{ParameterEntry{1}}, ValueKinds::Language(), {}});
  for (auto& use : wrong.uses) {
    const auto* edge = use.incoming ? std::get_if<EdgeId>(&*use.incoming) : nullptr;
    if (use.owner == decltype(use.owner){region.merged} && edge && *edge == region.keep_input)
      use.operand = other;
  }
  Check(FindLogicalRegions(wrong, control).empty());
  wrong = graph;
  wrong.values[region.left.value].domain = ValueKinds::Hole();
  Check(FindLogicalRegions(wrong, control).empty());
  wrong = graph;
  // A second observer of the predicate prevents absorbing its evaluation.
  for (auto& op : wrong.ops) if (std::holds_alternative<ValueReturn>(op.operation))
    wrong.uses[op.first_use.value].operand = *wrong.ops[region.boolean.value].result;
  Check(FindLogicalRegions(wrong, control).empty());
  wrong = graph; ++wrong.revision.value;
  Check(FindLogicalRegions(wrong, control).empty());
}
int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--sources") {
    std::cout << "[" << Source(Diamond(false)) << "," << Source(Diamond(true)) << "," << Source(Diamond(false, 1, false, true))
              << "," << Source(Diamond(true, 1, false, true))
              << "," << Source(Diamond(false, 1, false, true, false, true))
              << "," << Source(Diamond(true, 1, false, true, false, true)) << "]\n";
    return 0;
  }
  for (const bool negate : {false, true}) for (const bool direct : {false, true}) {
    for (const unsigned count : {1U, 200U, 201U, 402U}) {
      const auto graph = Diamond(negate, count, direct);
      const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
      const auto regions = FindLogicalRegions(graph, control);
      Check(regions.size() == count);
      for (const auto& region : regions) {
        Check(region.operation == (negate ? TreeLogical::kAnd : TreeLogical::kOr));
        Check(region.keep.has_value() != direct);
        Check(graph.values[region.merged.value].life == analysis::ValueLife::kLive);
      }
    }
  }
  for (const bool negate : {false, true}) for (const bool direct : {false, true})
    Check(Source(Diamond(negate, 1, direct)).find("$value") == std::string::npos);
  for (const unsigned count : {200U, 201U, 402U}) Check(!Source(Diamond(false, count)).empty());
  Check(!Source(Diamond(true, 1, false, true)).empty());
  Check(!Source(Diamond(true, 1, false, true, false, true)).empty());
  PhiInputs();
  Negatives();
}
