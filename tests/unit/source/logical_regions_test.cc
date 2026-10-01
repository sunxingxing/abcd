#include "../../../src/source/logical_regions.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/call_plan.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/family_print.h"
#include "../../../src/source/tree_checks.h"
#include <sstream>
#include "../../support/analysis/ssa_fixture.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"

using namespace abcd;
using namespace abcd::source;
using abcd::test::Check;
namespace {
analysis::ValueProgram Import(const std::vector<std::uint8_t>& bytes, std::uint32_t locals) {
  const auto body = test::Body(bytes);
  using namespace analysis;
  const auto method_result = BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, locals + 1);
  Check(std::holds_alternative<SemanticMethod>(method_result));
  const auto& method = std::get<SemanticMethod>(method_result);
  const auto proof_result = VerifySemanticSource(body, method, locals, 1);
  Check(std::holds_alternative<SemanticProof>(proof_result));
  const auto& proof = std::get<SemanticProof>(proof_result);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(locals, 1, {false, false, false, false}));
  auto imported = ImportNormalValues(body, method, proof, frame);
  Check(std::holds_alternative<ValueProgram>(imported));
  auto graph = std::get<ValueProgram>(std::move(imported));
  Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph, graph.revision)));
  return graph;
}
analysis::ValueProgram OptionalAccess(bool observable_fallback = false, unsigned count = 1) {
  // Two strict nullish checks share one undefined-producing arm. This is the
  // real optional-access CFG shape, independent of UI classes/captures.
  std::vector<std::uint8_t> bytes{0x01, 0x27, 0, 1, 0x4f, 0,
      0x00, 0x27, 0, 1, 0x4f, 0, 0x4d, 0};
  const auto fallback = bytes.size();
  bytes.push_back(0x00);
  if (observable_fallback) bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kDEBUGGER));
  const auto skip = bytes.size(); bytes.insert(bytes.end(), {0x4d, 0});
  const auto read = bytes.size(); bytes.insert(bytes.end(), {0x60, 1, 0x42, 0, 0, 0});
  const auto join = bytes.size(); bytes.insert(bytes.end(), {0x61, 1});
  bytes[5] = static_cast<std::uint8_t>(fallback - 4);
  bytes[11] = static_cast<std::uint8_t>(fallback - 10);
  bytes[13] = static_cast<std::uint8_t>(read - 12);
  bytes[skip + 1] = static_cast<std::uint8_t>(join - skip);
  std::vector<std::uint8_t> chain;
  for (unsigned i = 0; i < count; ++i) chain.insert(chain.end(), bytes.begin(), bytes.end());
  chain.insert(chain.end(), {0x60, 1, 0x64});
  return Import(chain, 1);
}
analysis::ValueProgram BufferedOptionalAccess(bool call = false, bool method_first = false) {
  // Buffered {probe: null} is allocated before the optional RHS, then the
  // existing own field is initialized after its branch/phi join.
  std::vector<std::uint8_t> bytes{0x07, 0, 0, 0, 0x61, 0,
      0x01, 0x27, 0, 2, 0x4f, 10, 0x00, 0x27, 0, 2, 0x4f, 4, 0x4d, 5,
      0x00, 0x4d, 8, 0x60, 2, 0x42, 0, 0, 0};
  const auto wire = static_cast<std::uint16_t>(abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8);
  bytes.push_back(static_cast<std::uint8_t>(wire));
  if (wire > UINT8_MAX) bytes.push_back(static_cast<std::uint8_t>(wire >> 8));
  bytes.insert(bytes.end(), {0, 0, 0, 0, 0x60, 0, 0x64});
  if (!call) return Import(bytes, 2);
  // Change the original parameter slot to r3, retaining r0 for the object and
  // r1 for the callee. These are decoded instructions, not a handcrafted tree.
  bytes[9] = 3; bytes[15] = 3; bytes[24] = 3;
  bytes.resize(bytes.size() - 3); // Replace the object-return suffix with a call.
  const std::vector<std::uint8_t> method{0x60, 3, 0x42, 0, 1, 0, 0x61, 1};
  if (method_first) bytes.insert(bytes.begin(), method.begin(), method.end());
  else bytes.insert(bytes.end(), method.begin(), method.end());
  bytes.insert(bytes.end(), {0x60, 1,
      static_cast<std::uint8_t>(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, 3, 0, 0x64});
  return Import(bytes, 3);
}
analysis::ValueProgram Diamond(bool negate, unsigned count = 1, bool direct = false, bool getter = false, bool barrier = false, bool invoke = false, bool left_getter = false) {
  // return arg || [] (or arg && []), retaining the compiler's empty keep arm.
  std::vector<std::uint8_t> bytes;
  for (unsigned i = 0; i < count; ++i) {
    std::vector<std::uint8_t> diamond{0x60, 2};
    if (left_getter) diamond.insert(diamond.end(), {0x42, 0, 1, 0});
    diamond.insert(diamond.end(), {0x61, 1, 0xfb, 0x13, 0});
    const auto branch_pc = diamond.size(), delta_operand = branch_pc + 1;
    diamond.insert(diamond.end(), {static_cast<std::uint8_t>(negate ? 0x4f : 0x51), 0});
    if (getter) {
      diamond.insert(diamond.end(), {0x60, 2, 0x42, 0, 0, 0});
      if (invoke) diamond.insert(diamond.end(), {0x2d, 0, 2});
    }
    else diamond.insert(diamond.end(), {0x05, 0});
    if (barrier) diamond.push_back(static_cast<std::uint8_t>(abc::Opcode::kDEBUGGER));
    diamond.insert(diamond.end(), {static_cast<std::uint8_t>(direct ? 0x61 : 0x4d),
        static_cast<std::uint8_t>(direct ? 1 : 4)});
    // Branch offsets target the preserved-value load, or the shared reload.
    diamond[delta_operand] = static_cast<std::uint8_t>(diamond.size() - branch_pc);
    diamond.insert(diamond.end(), {0x60, 1, 0x61, 2});
    bytes.insert(bytes.end(), diamond.begin(), diamond.end());
  }
  bytes.insert(bytes.end(), {0x60, 2, 0x64});
  return Import(bytes, 2);
}
analysis::ValueProgram CallDiamond(bool negate, bool method_first, bool barrier = false, bool shared = false) {
  // The same member read on opposite sides of argument selection is observably
  // different. Keep both bytecode forms as the cross-region integration gate.
  const std::vector<std::uint8_t> method{0x60, 3, 0x42, 0, 0, 0, 0x61, 0};
  std::vector<std::uint8_t> bytes;
  if (method_first) bytes.insert(bytes.end(), method.begin(), method.end());
  bytes.insert(bytes.end(), {0x60, 3, 0x42, 0, 1, 0, 0x61, 1});
  if (shared) bytes.insert(bytes.end(), {0x61, 2});
  if (barrier) bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kDEBUGGER));
  bytes.insert(bytes.end(), {0xfb, 0x13, 0});
  const auto branch_pc = bytes.size();
  bytes.insert(bytes.end(), {static_cast<std::uint8_t>(negate ? 0x4f : 0x51), 0, 0x05, 0, 0x61, 1});
  bytes[branch_pc + 1] = static_cast<std::uint8_t>(bytes.size() - branch_pc);
  if (!method_first) bytes.insert(bytes.end(), method.begin(), method.end());
  bytes.insert(bytes.end(), {0x60, 0});
  if (shared) bytes.insert(bytes.end(), {static_cast<std::uint8_t>(abc::Opcode::kCALLTHIS2_IMM8_V8_V8_V8), 0, 3, 1, 2});
  else bytes.insert(bytes.end(), {static_cast<std::uint8_t>(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, 3, 1});
  bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURN));
  return Import(bytes, 3);
}

}
std::string Source(const analysis::ValueProgram& graph, bool compose = true) {
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  const auto regions = FindLogicalRegions(graph, control);
  if (compose) for (const auto& region : regions) {
    const auto value = std::get<analysis::ValueId>(region.right);
    const auto producer = std::get<analysis::OpResult>(graph.values[value.value].origin).op;
    Check(evaluation.operations[producer.value].kind == EvaluationKind::kPhiInput);
    Check(!evaluation.operations[producer.value].temporary);
  }
  FunctionSyntax syntax{};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"value", u"probe", u"items"}; syntax.parameters.push_back({NameId{0}, NameEvidence::kDebugRange});
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (const auto* read = std::get_if<analysis::ValueNamedRead>(&graph.ops[i].operation))
      constants[i] = NameId{read->name.value + 1};
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (const auto* allocation = std::get_if<analysis::ValueObjectAllocation>(&graph.ops[i].operation))
      if (allocation->literal) constants[i] = AggregateConstant{0, 1};
    if (const auto* write = std::get_if<analysis::ValueOwnWrite>(&graph.ops[i].operation)) constants[i] = NameId{write->name.value + 1};
  }
  const std::vector<ConstantProperty> properties{{NameId{1}, NullLiteral{}}};
  const ExpressionConstants view{constants, 3, 0, {}, properties};
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), view);
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, view)));
  if (!regions.empty()) {
    auto wrong_operator = tree;
    bool composed = false;
    for (auto& node : wrong_operator.expressions) if (auto* logical = std::get_if<LogicalExpr>(&node.expression)) {
      logical->operation = logical->operation == TreeLogical::kOr ? TreeLogical::kAnd : TreeLogical::kOr;
      composed = true; break;
    }
    Check(composed == compose);
    if (compose) Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong_operator, view)));
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
void CallArguments() {
  using namespace analysis;
  for (const bool negate : {false, true}) {
    auto graph = CallDiamond(negate, true);
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
    TreeCheckBudget budget({});
    const auto logicals = FindLogicalExpressions(graph, control, evaluation, budget);
    Check(logicals.size() == 1);
    Check(logicals.front().operation == (negate ? TreeLogical::kAnd : TreeLogical::kOr));
    // The native argument and the earlier callee now share a verified effect
    // interval; moving the callee alone remains an invalid plan.
    Check(std::holds_alternative<CallPlan>(BuildCallPlan(graph, evaluation)));
    Check(Source(graph).find("$phi") == std::string::npos);
    for (const auto& op : graph.ops) if (std::holds_alternative<ValueInvoke>(op.operation)) {
      const auto callee = std::get<ValueId>(graph.uses[op.first_use.value].operand);
      const auto producer = std::get<OpResult>(graph.values[callee.value].origin).op;
      Check(graph.ops[producer.value].block == logicals.front().header);
      Check(op.block == logicals.front().join);
      Check(evaluation.operations[producer.value].kind == EvaluationKind::kInline);
      auto forged = evaluation;
      forged.operations[producer.value].consumer = graph.ops[logicals.front().boolean.value].first_use;
      Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, forged)));
    }
    SemanticBudget allowance{}; allowance.work = evaluation.work;
    Check(std::holds_alternative<EvaluationPlan>(BuildEvaluationPlan(graph, allowance)));
    --allowance.work;
    Check(std::get<EvaluationError>(BuildEvaluationPlan(graph, allowance)) == EvaluationError::kWorkLimit);
    allowance = {}; allowance.bytes = 0;
    Check(std::get<EvaluationError>(BuildEvaluationPlan(graph, allowance)) == EvaluationError::kResourceLimit);
    auto wrong_value = evaluation;
    wrong_value.logical_values[logicals.front().left.value] = logicals.front().branch;
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, wrong_value)));
    auto missing = evaluation; missing.logical_values.clear();
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, missing)));
    const auto late_graph = CallDiamond(negate, false);
    const auto late_control = std::get<ControlPlan>(BuildControlPlan(late_graph));
    const auto late_region = FindLogicalRegions(late_graph, late_control).front();
    auto late = std::get<EvaluationPlan>(BuildEvaluationPlan(late_graph));
    Check(late.logical_values.empty());
    late.logical_values.resize(late_graph.values.size());
    late.logical_values[late_region.merged.value] = late_region.branch;
    late.operations[late_region.branch.value] = {EvaluationKind::kLogicalValue,
        late_graph.values[late_region.merged.value].first_use, {}};
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(late_graph, late)));
    // The later member getter already has a legal same-block reference; retain
    // the logical argument snapshot so its effects still precede that getter.
    Check(Source(CallDiamond(negate, false)).find("$value") == std::string::npos);
  }
}
void LogicalLefts() {
  using namespace analysis;
  for (const bool negate : {false, true}) {
    const auto graph = CallDiamond(negate, false);
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
    const auto region = FindLogicalRegions(graph, control).front();
    const auto left = std::get<OpResult>(graph.values[region.left.value].origin).op;
    Check(evaluation.operations[left.value].kind == EvaluationKind::kLogicalLeft);
    Check(!evaluation.operations[left.value].temporary);
    auto forged = evaluation;
    forged.operations[left.value].consumer = graph.ops[region.branch.value].first_use;
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, forged)));
    forged = evaluation;
    forged.operations[left.value].kind = EvaluationKind::kInline;
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, forged)));
    for (const bool shared : {false, true}) {
      const auto excluded = CallDiamond(negate, false, !shared, shared);
      const auto excluded_control = std::get<ControlPlan>(BuildControlPlan(excluded));
      const auto excluded_region = FindLogicalRegions(excluded, excluded_control).front();
      const auto producer = std::get<OpResult>(excluded.values[excluded_region.left.value].origin).op;
      auto plan = std::get<EvaluationPlan>(BuildEvaluationPlan(excluded));
      Check(plan.operations[producer.value].kind == EvaluationKind::kRoot);
      Check(Source(excluded).find("$value") != std::string::npos);
      plan.operations[producer.value] = {EvaluationKind::kLogicalLeft,
          excluded.ops[excluded_region.boolean.value].first_use, {}};
      Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(excluded, plan)));
    }
  }
  for (const unsigned count : {1U, 200U, 201U, 402U}) {
    const auto graph = Diamond(false, count, false, false, false, false, true);
    const auto plan = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
    unsigned selected = 0;
    for (const auto& op : plan.operations) selected += op.kind == EvaluationKind::kLogicalLeft;
    Check(selected == count);
    Check(Source(graph).find("$value") == std::string::npos);
    if (count == 201) {
      TreeCheckBudget measured({});
      const auto inputs = FindLogicalLeftInputs(graph, plan, measured);
      Check(inputs.size() == graph.ops.size());
      for (const bool bytes : {false, true}) {
        TreeCheckBudget limited({measured.Bytes() - (bytes ? 1 : 0), measured.Work() - (bytes ? 0 : 1)});
        bool rejected = false;
        try { static_cast<void>(FindLogicalLeftInputs(graph, plan, limited)); }
        catch (TreeStructureError error) {
          rejected = true;
          Check(error == (bytes ? TreeStructureError::kResourceLimit : TreeStructureError::kWorkLimit));
        }
        Check(rejected);
      }
    }
  }
}
void SelectionBudget() {
  const auto graph = Diamond(false, 201);
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  TreeCheckBudget budget({});
  Check(FindLogicalExpressions(graph, control, evaluation, budget).size() == 201);
  for (const bool bytes : {false, true}) {
    TreeCheckBudget limited({budget.Bytes() - (bytes ? 1 : 0), budget.Work() - (bytes ? 0 : 1)});
    bool rejected = false;
    try { static_cast<void>(FindLogicalExpressions(graph, control, evaluation, limited)); }
    catch (TreeStructureError error) {
      rejected = true;
      Check(error == (bytes ? TreeStructureError::kResourceLimit : TreeStructureError::kWorkLimit));
    }
    Check(rejected);
  }
  evaluation.operations.clear();
  TreeCheckBudget fresh({});
  Check(FindLogicalExpressions(graph, control, evaluation, fresh).empty());
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
  if (argc == 2 && std::string_view(argv[1]) == "--buffered-call-source") {
    std::cout << Source(BufferedOptionalAccess(true, true)) << "\n";
    return 0;
  }
  // Until region composition owns the whole argument, reject a saved member
  // Reference rather than re-reading the property after the optional getter.
  const auto buffered_call = BufferedOptionalAccess(true, true);
  const auto buffered_evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(buffered_call));
  const auto buffered_binding = BuildCallPlan(buffered_call, buffered_evaluation);
  Check(std::holds_alternative<CallPlanError>(buffered_binding));
  Check(std::get<CallPlanError>(buffered_binding) == CallPlanError::kReference);
  Check(!Source(BufferedOptionalAccess(true, false)).empty());
  Check(!Source(BufferedOptionalAccess()).empty());
  const auto optional = OptionalAccess();
  Check(optional.cfg_revision.value == 1);
  Check(!Source(optional).empty());
  auto repeated = optional;
  const auto revision = repeated.revision;
  const auto again = std::get<analysis::ValueSimplifyStats>(analysis::SimplifyValues(repeated, revision));
  Check(again.specialized_edges == 0 && repeated.revision == revision);
  for (unsigned count : {2U, 3U, 200U, 201U}) Check(!Source(OptionalAccess(false, count)).empty());
  const auto observable = OptionalAccess(true);
  Check(observable.cfg_revision.value == 0);
  Check(std::get<ControlError>(BuildControlPlan(observable)) == ControlError::kCrossEntry);

  if (argc == 2 && std::string_view(argv[1]) == "--sources") {
    std::cout << "[" << Source(Diamond(false)) << "," << Source(Diamond(true)) << "," << Source(Diamond(false, 1, false, true))
              << "," << Source(Diamond(true, 1, false, true))
              << "," << Source(Diamond(false, 1, false, true, false, true))
              << "," << Source(Diamond(true, 1, false, true, false, true))
              << "," << Source(CallDiamond(false, false)) << "," << Source(CallDiamond(true, false))
              << "," << Source(CallDiamond(false, false, false, true))
              << "," << Source(CallDiamond(true, false, false, true))
              << "," << Source(CallDiamond(false, true)) << "," << Source(CallDiamond(true, true)) << "," << Source(optional) << "," << Source(OptionalAccess(false, 2)) << "," << Source(BufferedOptionalAccess()) << "," << Source(BufferedOptionalAccess(true, false)) << "]\n";
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
  Check(Source(Diamond(false)).find(" || ") != std::string::npos);
  Check(Source(Diamond(true)).find(" && ") != std::string::npos);
  const auto uncomposed = Source(Diamond(false, 1, true, false, true), false);
  Check(uncomposed.find("if (") != std::string::npos && uncomposed.find("debugger;") != std::string::npos);
  Check(uncomposed.find(" || ") == std::string::npos);
  CallArguments();
  LogicalLefts();
  SelectionBudget();
  PhiInputs();
  Negatives();
}
