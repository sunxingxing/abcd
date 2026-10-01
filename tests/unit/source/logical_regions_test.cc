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
analysis::ValueProgram OptionalAccess(bool observable_fallback = false, unsigned count = 1, bool distinct_fallback = false, bool corridor_effect = false, bool header_effect = false) {
  // Two strict nullish checks share one undefined-producing arm. This is the
  // real optional-access CFG shape, independent of UI classes/captures.
  std::vector<std::uint8_t> bytes{0x01, 0x27, 0, 1, 0x4f, 0,
      0x00, 0x27, 0, 1, 0x4f, 0, 0x4d, 0};
  const auto fallback = bytes.size();
  bytes.push_back(0x00);
  if (observable_fallback) bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kDEBUGGER));
  const auto skip = bytes.size(); bytes.insert(bytes.end(), {0x4d, 0});
  const auto null_fallback = bytes.size();
  if (distinct_fallback) bytes.insert(bytes.end(), {0x01, 0x4d, 0});
  const auto read = bytes.size(); bytes.insert(bytes.end(), {0x60, 1, 0x42, 0, 0, 0});
  const auto join = bytes.size(); bytes.insert(bytes.end(), {0x61, 1});
  bytes[5] = static_cast<std::uint8_t>((distinct_fallback ? null_fallback : fallback) - 4);
  bytes[11] = static_cast<std::uint8_t>(fallback - 10);
  bytes[13] = static_cast<std::uint8_t>(read - 12);
  bytes[skip + 1] = static_cast<std::uint8_t>(join - skip);
  if (distinct_fallback) bytes[null_fallback + 2] = static_cast<std::uint8_t>(join - null_fallback - 1);
  if (corridor_effect) {
    bytes.insert(bytes.begin() + 12, static_cast<std::uint8_t>(abc::Opcode::kDEBUGGER));
    ++bytes[5]; ++bytes[11];
  }
  if (header_effect) {
    bytes.insert(bytes.begin() + 6, static_cast<std::uint8_t>(abc::Opcode::kDEBUGGER));
    ++bytes[5];
  }
  std::vector<std::uint8_t> chain;
  for (unsigned i = 0; i < count; ++i) chain.insert(chain.end(), bytes.begin(), bytes.end());
  chain.insert(chain.end(), {0x60, 1, 0x64});
  return Import(chain, 1);
}
analysis::ValueProgram NestedSelections(unsigned count) {
  std::vector<std::uint8_t> bytes;
  std::vector<std::size_t> exits;
  for (unsigned i = 0; i < count; ++i) {
    bytes.insert(bytes.end(), {0x00, 0x27, 0, 1});
    const auto branch = bytes.size(); bytes.insert(bytes.end(), {0x51, 0, 0x00});
    exits.push_back(bytes.size());
    bytes.insert(bytes.end(), {static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM16), 0, 0});
    bytes[branch + 1] = static_cast<std::uint8_t>(bytes.size() - branch);
  }
  bytes.insert(bytes.end(), {0x60, 1, 0x42, 0, 0, 0});
  const auto join = bytes.size(); bytes.push_back(0x64);
  for (const auto exit : exits) {
    Check(join - exit <= INT16_MAX);
    const auto delta = static_cast<std::uint16_t>(join - exit);
    bytes[exit + 1] = static_cast<std::uint8_t>(delta);
    bytes[exit + 2] = static_cast<std::uint8_t>(delta >> 8);
  }
  return Import(bytes, 1);
}
analysis::ValueProgram ConditionalDiamond(bool negate = false, unsigned count = 1, bool barrier = false, bool raw_predicate = false) {
  // value.probe ? value.items : [], with two independently evaluated arms.
  std::vector<std::uint8_t> bytes{0x60, 1, 0x42, 0, 0, 0, 0xfb, 0x13, 0,
      static_cast<std::uint8_t>(negate ? 0x51 : 0x4f), 10,
      0x60, 1, 0x42, 0, 1, 0, 0x4d, 4, 0x05, 0, 0x64};
  if (barrier) {
    bytes.insert(bytes.begin() + 17, static_cast<std::uint8_t>(abc::Opcode::kDEBUGGER));
    ++bytes[10];
  }
  if (raw_predicate) bytes.erase(bytes.begin() + 6, bytes.begin() + 9);
  if (count == 1) return Import(bytes, 1);
  bytes.pop_back();
  bytes.insert(bytes.end(), {0x61, 0, 0x60, 1, 0x42, 0, 1, 0,
      static_cast<std::uint8_t>(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, 1, 0});
  std::vector<std::uint8_t> repeated;
  for (unsigned i = 0; i < count; ++i) repeated.insert(repeated.end(), bytes.begin(), bytes.end());
  repeated.push_back(0x64);
  return Import(repeated, 1);
}
analysis::ValueProgram ConditionalCall(bool method_first) {
  const std::vector<std::uint8_t> method{0x60, 3, 0x42, 0, 1, 0, 0x61, 0};
  std::vector<std::uint8_t> bytes;
  if (method_first) bytes.insert(bytes.end(), method.begin(), method.end());
  const std::vector<std::uint8_t> select{0x60, 3, 0x42, 0, 0, 0, 0xfb, 0x13, 0,
      0x4f, 10, 0x60, 3, 0x42, 0, 0, 0, 0x4d, 4, 0x05, 0, 0x61, 1};
  bytes.insert(bytes.end(), select.begin(), select.end());
  if (!method_first) bytes.insert(bytes.end(), method.begin(), method.end());
  bytes.insert(bytes.end(), {0x60, 0, static_cast<std::uint8_t>(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, 3, 1, 0x64});
  return Import(bytes, 3);
}
analysis::ValueProgram OptionalCall(bool method_first) {
  const std::vector<std::uint8_t> method{0x60, 3, 0x42, 0, 1, 0, 0x61, 0};
  std::vector<std::uint8_t> bytes;
  if (method_first) bytes.insert(bytes.end(), method.begin(), method.end());
  const std::vector<std::uint8_t> select{0x01, 0x27, 0, 3, 0x4f, 10,
      0x00, 0x27, 0, 3, 0x4f, 4, 0x4d, 5, 0x00, 0x4d, 8, 0x60, 3, 0x42, 0, 0, 0, 0x61, 1};
  bytes.insert(bytes.end(), select.begin(), select.end());
  if (!method_first) bytes.insert(bytes.end(), method.begin(), method.end());
  bytes.insert(bytes.end(), {0x60, 0, static_cast<std::uint8_t>(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, 3, 1, 0x64});
  return Import(bytes, 3);
}
enum class BufferedBarrier { kNone, kBeforeAllocation, kAfterAllocation, kBeforeCall };
analysis::ValueProgram BufferedOptionalAccess(bool call = false, bool method_first = false,
    BufferedBarrier barrier = BufferedBarrier::kNone, bool shared = false) {
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
  const auto debugger = static_cast<std::uint8_t>(abc::Opcode::kDEBUGGER);
  if (barrier == BufferedBarrier::kBeforeCall) bytes.push_back(debugger);
  const auto allocation = method_first ? method.size() : 0;
  if (barrier == BufferedBarrier::kBeforeAllocation) bytes.insert(bytes.begin() + allocation, debugger);
  if (barrier == BufferedBarrier::kAfterAllocation) bytes.insert(bytes.begin() + allocation + 6, debugger);
  bytes.insert(bytes.end(), {0x60, 1});
  if (shared) bytes.insert(bytes.end(), {static_cast<std::uint8_t>(abc::Opcode::kCALLTHIS2_IMM8_V8_V8_V8), 0, 3, 0, 0});
  else bytes.insert(bytes.end(), {static_cast<std::uint8_t>(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, 3, 0});
  bytes.push_back(0x64);
  return Import(bytes, 3);
}
analysis::ValueProgram BufferedSelections(unsigned count = 2, bool barrier = false, bool distinct = true, unsigned suffix = 0, bool suffix_barrier = false) {
  // Both distinct keys and repeated own definitions must retain every RHS.
  std::vector<std::uint8_t> bytes{0x60, 3, 0x42, 0, 1, 0, 0x61, 1,
      0x07, 0, 0, 0, 0x61, 0};
  const auto wire = static_cast<std::uint16_t>(abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8);
  for (unsigned i = 0; i < count; ++i) {
    if (barrier && i) bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kDEBUGGER));
    bytes.insert(bytes.end(), {0x01, 0x27, 0, 3, 0x4f, 10, 0x00, 0x27, 0, 3, 0x4f, 4, 0x4d, 5,
        0x00, 0x4d, 8, 0x60, 3, 0x42, 0, 0, 0});
    const std::uint8_t name = distinct && i % 2 ? 2 : 0;
    bytes[bytes.size() - 2] = name;
    for (unsigned depth = 0; depth < suffix; ++depth) {
      bytes.insert(bytes.end(), {0x61, 2, static_cast<std::uint8_t>(abc::Opcode::kLDA_STR_ID16), 0, 0,
          static_cast<std::uint8_t>(abc::Opcode::kADD2_IMM8_V8), 0, 2});
    }
    if (suffix_barrier) bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kDEBUGGER));
    bytes.push_back(static_cast<std::uint8_t>(wire));
    if (wire > UINT8_MAX) bytes.push_back(static_cast<std::uint8_t>(wire >> 8));
    bytes.insert(bytes.end(), {0, name, 0, 0});
  }
  bytes.insert(bytes.end(), {0x60, 1, static_cast<std::uint8_t>(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, 3, 0, 0x64});
  return Import(bytes, 3);
}
analysis::ValueProgram GetterSelections(unsigned count = 2, bool barrier = false, bool call = true, bool trailing_literal = false) {
  std::vector<std::uint8_t> bytes{0x60, 3, 0x42, 0, 1, 0, 0x61, 1,
      0x07, 0, 0, 0, 0x61, 0, 0x60, 3, 0x42, 0, 0, 0, 0x61, 2};
  if (!call) bytes.erase(bytes.begin(), bytes.begin() + 14);
  std::vector<std::pair<std::size_t, std::size_t>> branches;
  const auto emit = [&](abc::Opcode opcode) {
    const auto wire = static_cast<std::uint16_t>(opcode);
    bytes.push_back(static_cast<std::uint8_t>(wire));
    if (wire > UINT8_MAX) bytes.push_back(static_cast<std::uint8_t>(wire >> 8));
  };
  const auto jump = [&](abc::Opcode opcode) {
    const auto pc = bytes.size(); emit(opcode); const auto operand = bytes.size();
    bytes.insert(bytes.end(), sizeof(std::int32_t), 0);
    return std::pair{pc, operand};
  };
  const auto patch = [&](auto branch, std::size_t target) {
    const auto delta = static_cast<std::uint32_t>(target - branch.first);
    for (unsigned i = 0; i < sizeof(delta); ++i) bytes[branch.second + i] = static_cast<std::uint8_t>(delta >> (i * 8));
  };
  for (unsigned i = 0; i < count; ++i) {
    if (barrier) emit(abc::Opcode::kDEBUGGER);
    bytes.insert(bytes.end(), {0x01, 0x27, 0, 2}); branches.push_back(jump(abc::Opcode::kJEQZ_IMM32));
    bytes.insert(bytes.end(), {0x00, 0x27, 0, 2}); branches.push_back(jump(abc::Opcode::kJEQZ_IMM32));
    bytes.insert(bytes.end(), {0x60, 2, 0x42, 0, 0, 0, 0x61, 2});
  }
  bytes.insert(bytes.end(), {0x60, 2});
  const auto end = jump(abc::Opcode::kJMP_IMM32);
  const auto fallback = bytes.size(); bytes.push_back(0x00);
  const auto fallback_end = jump(abc::Opcode::kJMP_IMM32);
  for (const auto branch : branches) patch(branch, fallback);
  patch(end, bytes.size()); patch(fallback_end, bytes.size());
  if (!call) { bytes.push_back(0x64); return Import(bytes, 3); }
  emit(abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8); bytes.insert(bytes.end(), {0, 0, 0, 0});
  bytes.insert(bytes.end(), {0x60, 1, static_cast<std::uint8_t>(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, 3, 0});
  if (trailing_literal) {
    bytes.insert(bytes.end(), {0x61, 2, 0x60, 3, 0x42, 0, 1, 0, 0x61, 1,
        0x07, 0, 0, 0, 0x61, 0, 0x60, 3, 0x42, 0, 0, 0});
    emit(abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8); bytes.insert(bytes.end(), {0, 0, 0, 0});
    bytes.insert(bytes.end(), {0x60, 1, static_cast<std::uint8_t>(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, 3, 0, 0x60, 2});
  }
  bytes.push_back(0x64);
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
  syntax.names = {u"value", u"probe", u"items", u"other"}; syntax.parameters.push_back({NameId{0}, NameEvidence::kDebugRange});
  syntax.texts = {u"px"};
  std::vector<ConstantProperty> properties{{NameId{1}, NullLiteral{}}};
  for (const auto& op : graph.ops) {
    const auto* field = std::get_if<analysis::ValueOwnWrite>(&op.operation);
    if (field && field->name.value == 2) { properties.push_back({NameId{3}, NullLiteral{}}); break; }
  }
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (const auto* read = std::get_if<analysis::ValueNamedRead>(&graph.ops[i].operation))
      constants[i] = NameId{read->name.value + 1};
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (const auto* allocation = std::get_if<analysis::ValueObjectAllocation>(&graph.ops[i].operation))
      if (allocation->literal) constants[i] = AggregateConstant{0, static_cast<std::uint32_t>(properties.size())};
    if (const auto* write = std::get_if<analysis::ValueOwnWrite>(&graph.ops[i].operation)) constants[i] = NameId{write->name.value + 1};
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (std::holds_alternative<analysis::ValueString>(graph.ops[i].operation)) constants[i] = TextId{0};
  const ExpressionConstants view{constants, 4, 1, {}, properties};
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), view);
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, view)));
  for (const auto& placement : tree.operations) {
    if (evaluation.operations[placement.operation.value].kind != EvaluationKind::kPredicateSnapshot) continue;
    const auto raw = std::get<ExprId>(placement.node);
    for (std::size_t i = 0; i < tree.expressions.size(); ++i) {
      const auto* assignment = std::get_if<AssignmentExpr>(&tree.expressions[i].expression);
      if (!assignment || assignment->value != raw) continue;
      auto wrong = tree;
      std::get<AssignmentExpr>(wrong.expressions[i].expression).target = SymbolTarget{tree.parameters.front().symbol};
      Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong, view)));
      break;
    }
    break;
  }
  for (std::size_t i = 0; i < evaluation.logical_objects.size(); ++i) {
    if (!evaluation.logical_objects[i]) continue;
    for (const auto& placement : tree.operations) {
      if (placement.operation != *evaluation.logical_objects[i]) continue;
      auto wrong = tree;
      const auto property = std::get<PropertyId>(placement.node);
      wrong.properties[property.value].origin = TreeOriginId{graph.ops[i].origin.value};
      Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong, view)));
    }
  }
  for (std::size_t i = 0; i < tree.expressions.size(); ++i) {
    if (!std::holds_alternative<ConditionalExpr>(tree.expressions[i].expression)) continue;
    auto wrong = tree;
    auto& conditional = std::get<ConditionalExpr>(wrong.expressions[i].expression);
    std::swap(conditional.consequent, conditional.alternate);
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong, view)));
    wrong = tree;
    wrong.expressions[i].origin = wrong.expressions[std::get<ConditionalExpr>(wrong.expressions[i].expression).condition.value].origin;
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong, view)));
    break;
  }
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
    const auto logicals = FindSelectionExpressions(graph, control, evaluation, budget);
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
  Check(FindSelectionExpressions(graph, control, evaluation, budget).size() == 201);
  for (const bool bytes : {false, true}) {
    TreeCheckBudget limited({budget.Bytes() - (bytes ? 1 : 0), budget.Work() - (bytes ? 0 : 1)});
    bool rejected = false;
    try { static_cast<void>(FindSelectionExpressions(graph, control, evaluation, limited)); }
    catch (TreeStructureError error) {
      rejected = true;
      Check(error == (bytes ? TreeStructureError::kResourceLimit : TreeStructureError::kWorkLimit));
    }
    Check(rejected);
  }
  evaluation.operations.clear();
  TreeCheckBudget fresh({});
  Check(FindSelectionExpressions(graph, control, evaluation, fresh).empty());
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
  if (argc == 2 && std::string_view(argv[1]) == "--getter-selections-source") {
    std::cout << Source(GetterSelections()) << "\n";
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--buffered-suffix-source") {
    std::cout << Source(BufferedSelections(2, false, true, 2)) << "\n";
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--buffered-selections-source") {
    std::cout << Source(BufferedSelections()) << "\n";
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--buffered-call-source") {
    std::cout << Source(BufferedOptionalAccess(true, true)) << "\n";
    return 0;
  }
  for (unsigned count : {2U, 3U, 16U, 64U, 128U, 200U, 201U, 256U}) {
    const auto text = Source(BufferedSelections(count));
    Check(text.find("$value") == std::string::npos && text.find("$phi") == std::string::npos);
    std::size_t found = 0, offset = 0;
    while ((offset = text.find(" ? ", offset)) != std::string::npos) { ++found; offset += 3; }
    Check(found == 2 * count);
  }
  Check(Source(BufferedSelections(2, false, false)).find("$value") == std::string::npos);
  const auto interrupted = BufferedSelections(2, true);
  const auto interrupted_plan = std::get<EvaluationPlan>(BuildEvaluationPlan(interrupted));
  Check(interrupted_plan.logical_objects.empty());
  Check(std::get<CallPlanError>(BuildCallPlan(interrupted, interrupted_plan)) == CallPlanError::kReference);
  const auto multiple_fields = BufferedSelections();
  const auto multiple_plan = std::get<EvaluationPlan>(BuildEvaluationPlan(multiple_fields));
  for (const auto& object : multiple_plan.literals->objects) {
    if (object.count != 2) continue;
    auto wrong = multiple_plan;
    const auto first = wrong.literals->definitions[object.first];
    wrong.operations[first.value] = {EvaluationKind::kRoot, {}, {}};
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(multiple_fields, wrong)));
    wrong = multiple_plan; wrong.logical_objects[object.allocation.value] = first;
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(multiple_fields, wrong)));
  }
  for (unsigned depth : {1U, 2U, 16U, 64U, 200U, 201U}) {
    const auto text = Source(BufferedSelections(2, false, true, depth));
    Check(text.find("$value") == std::string::npos && text.find("$phi") == std::string::npos);
  }
  Check(Source(BufferedSelections(201, false, true, 1)).find("$value") == std::string::npos);
  const auto suffix_gap = BufferedSelections(2, false, true, 2, true);
  const auto suffix_gap_plan = std::get<EvaluationPlan>(BuildEvaluationPlan(suffix_gap));
  Check(suffix_gap_plan.logical_objects.empty());
  Check(std::get<CallPlanError>(BuildCallPlan(suffix_gap, suffix_gap_plan)) == CallPlanError::kReference);
  const auto suffix_graph = BufferedSelections(2, false, true, 2);
  const auto suffix_plan = std::get<EvaluationPlan>(BuildEvaluationPlan(suffix_graph));
  for (const auto& object : suffix_plan.literals->objects) for (unsigned i = 0; i < object.count; ++i) {
    const auto field = suffix_plan.literals->definitions[object.first + i];
    const auto value = std::get<analysis::ValueId>(suffix_graph.uses[suffix_graph.ops[field.value].first_use.value + 1].operand);
    const auto producer = std::get<analysis::OpResult>(suffix_graph.values[value.value].origin).op;
    auto wrong = suffix_plan; wrong.operations[producer.value].consumer = suffix_graph.ops[field.value].first_use;
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(suffix_graph, wrong)));
    wrong = suffix_plan; wrong.operations[producer.value] = {EvaluationKind::kRoot, {}, TemporaryReason::kEffectSnapshot};
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(suffix_graph, wrong)));
  }
  for (unsigned count : {2U, 3U, 16U, 64U, 200U, 201U}) {
    const auto getters = GetterSelections(count);
    const auto text = Source(getters);
    Check(text.find("if (") == std::string::npos && text.find("$phi") == std::string::npos);
    const auto plan = std::get<EvaluationPlan>(BuildEvaluationPlan(getters));
    unsigned snapshots = 0;
    for (std::size_t i = 0; i < plan.operations.size(); ++i) {
      if (plan.operations[i].kind != EvaluationKind::kPredicateSnapshot) continue;
      ++snapshots;
      if (count != 2) continue;
      auto wrong = plan; wrong.operations[i].temporary = TemporaryReason::kEffectSnapshot;
      Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(getters, wrong)));
      wrong = plan; wrong.operations[i].consumer.reset();
      Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(getters, wrong)));
    }
    Check(snapshots == count);
  }
  Check(!Source(GetterSelections(2, false, true, true)).empty());
  const auto getter_gap = GetterSelections(2, true);
  const auto getter_gap_plan = std::get<EvaluationPlan>(BuildEvaluationPlan(getter_gap));
  for (const auto& operation : getter_gap_plan.operations) Check(operation.kind != EvaluationKind::kPredicateSnapshot);
  Check(std::get<CallPlanError>(BuildCallPlan(getter_gap, getter_gap_plan)) == CallPlanError::kReference);
  // Native object arguments must retain the original early member Reference.
  const auto buffered_call = BufferedOptionalAccess(true, true);
  const auto buffered_evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(buffered_call));
  const auto buffered_binding = BuildCallPlan(buffered_call, buffered_evaluation);
  Check(std::holds_alternative<CallPlan>(buffered_binding));
  Check(Source(buffered_call).find("$value") == std::string::npos);
  Check(Source(buffered_call).find("value.items(({probe:") != std::string::npos);
  Check(!Source(BufferedOptionalAccess(true, false)).empty());
  Check(Source(BufferedOptionalAccess()).find("$value") == std::string::npos);
  for (const auto barrier : {BufferedBarrier::kBeforeAllocation, BufferedBarrier::kAfterAllocation,
                            BufferedBarrier::kBeforeCall}) {
    const auto blocked = BufferedOptionalAccess(true, true, barrier);
    const auto plan = std::get<EvaluationPlan>(BuildEvaluationPlan(blocked));
    Check(std::get<CallPlanError>(BuildCallPlan(blocked, plan)) == CallPlanError::kReference);
  }
  const auto shared_object = BufferedOptionalAccess(true, true, BufferedBarrier::kNone, true);
  const auto shared_plan = std::get<EvaluationPlan>(BuildEvaluationPlan(shared_object));
  Check(shared_plan.logical_objects.empty());
  Check(std::get<CallPlanError>(BuildCallPlan(shared_object, shared_plan)) == CallPlanError::kReference);
  Check(!buffered_evaluation.logical_objects.empty());
  for (std::size_t i = 0; i < buffered_evaluation.logical_objects.size(); ++i) {
    if (!buffered_evaluation.logical_objects[i]) continue;
    const auto field = *buffered_evaluation.logical_objects[i];
    auto forged = buffered_evaluation;
    forged.logical_objects.clear();
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(buffered_call, forged)));
    forged = buffered_evaluation; forged.logical_objects.resize(1);
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(buffered_call, forged)));
    forged = buffered_evaluation; forged.logical_objects[i] = analysis::OpId{static_cast<std::uint32_t>(i)};
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(buffered_call, forged)));
    forged = buffered_evaluation; forged.operations[i].consumer = buffered_call.ops[field.value].first_use;
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(buffered_call, forged)));
    forged = buffered_evaluation; forged.operations[field.value] = {EvaluationKind::kRoot, {}, {}};
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(buffered_call, forged)));
  }
  for (unsigned count : {2U, 3U, 200U, 201U}) {
    const auto text = Source(ConditionalDiamond(false, count));
    std::size_t found = 0, offset = 0;
    while ((offset = text.find(" ? ", offset)) != std::string::npos) { ++found; offset += 3; }
    Check(found == count);
  }
  const auto uncontracted = Source(ConditionalDiamond(false, 1, true));
  Check(uncontracted.find("debugger;") != std::string::npos && uncontracted.find(" ? ") == std::string::npos);
  const auto raw_predicate = ConditionalDiamond(false, 1, false, true);
  const auto raw_control = std::get<ControlPlan>(BuildControlPlan(raw_predicate));
  const auto raw_evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(raw_predicate));
  TreeCheckBudget raw_budget({});
  Check(FindSelectionExpressions(raw_predicate, raw_control, raw_evaluation, raw_budget).empty());
  Check(!Source(ConditionalCall(true)).empty());
  Check(!Source(ConditionalCall(false)).empty());
  Check(Source(ConditionalDiamond()).find(" ? ") != std::string::npos);
  Check(Source(ConditionalDiamond(true)).find(" ? ") != std::string::npos);
  Check(!Source(BufferedOptionalAccess()).empty());
  const auto optional = OptionalAccess();
  Check(optional.cfg_revision.value == 1);
  Check(Source(optional).find(" ? ") != std::string::npos);
  Check(Source(optional).find("if (") == std::string::npos);
  Check(Source(OptionalCall(true)).find("$phi") == std::string::npos);
  Check(!Source(OptionalCall(false)).empty());
  const auto optional_control = std::get<ControlPlan>(BuildControlPlan(optional));
  auto optional_evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(optional));
  TreeCheckBudget optional_budget({});
  const auto selected = FindSelectionExpressions(optional, optional_control, optional_evaluation, optional_budget);
  Check(selected.size() == 2 && selected.front().parent && !selected.front().complete_phi && selected.back().complete_phi);
  const auto& partial = selected.front();
  optional_evaluation.logical_values.resize(optional.values.size());
  optional_evaluation.logical_values[partial.merged.value] = partial.branch;
  optional_evaluation.operations[partial.branch.value] = {EvaluationKind::kLogicalValue,
      optional.values[partial.merged.value].first_use, {}};
  Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(optional, optional_evaluation)));
  Check(Source(OptionalAccess(false, 1, true)).find(" ? ") != std::string::npos);
  const auto corridor = Source(OptionalAccess(false, 1, false, true));
  Check(corridor.find("debugger;") != std::string::npos && corridor.find(" ? ") == std::string::npos);
  const auto guarded = Source(OptionalAccess(false, 1, false, false, true));
  Check(guarded.find("debugger;") != std::string::npos && guarded.find("if (") != std::string::npos &&
      guarded.find(" ? ") != std::string::npos);
  auto repeated = optional;
  const auto revision = repeated.revision;
  const auto again = std::get<analysis::ValueSimplifyStats>(analysis::SimplifyValues(repeated, revision));
  Check(again.specialized_edges == 0 && repeated.revision == revision);
  for (unsigned count : {2U, 3U, 200U, 201U}) {
    const auto text = Source(OptionalAccess(false, count));
    std::size_t found = 0, offset = 0;
    while ((offset = text.find(" ? ", offset)) != std::string::npos) { ++found; offset += 3; }
    Check(found == 2 * count);
    Check(text.find("if (") == std::string::npos);
  }
  for (unsigned count : {2U, 3U, 200U, 201U, 402U}) {
    const auto text = Source(NestedSelections(count));
    std::size_t found = 0, offset = 0;
    while ((offset = text.find(" ? ", offset)) != std::string::npos) { ++found; offset += 3; }
    Check(found == count && text.find("if (") == std::string::npos);
  }
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
              << "," << Source(CallDiamond(false, true)) << "," << Source(CallDiamond(true, true)) << "," << Source(optional) << "," << Source(OptionalAccess(false, 2)) << "," << Source(BufferedOptionalAccess()) << "," << Source(BufferedOptionalAccess(true, false)) << "," << Source(ConditionalDiamond()) << "," << Source(ConditionalDiamond(true)) << "," << Source(ConditionalCall(true)) << "," << Source(ConditionalCall(false)) << "," << Source(OptionalAccess(false, 1, true)) << "," << Source(OptionalCall(true)) << "," << Source(OptionalCall(false)) << "," << Source(NestedSelections(3)) << "," << Source(buffered_call) << "," << Source(BufferedSelections()) << "," << Source(BufferedSelections(2, false, false)) << "," << Source(BufferedSelections(2, false, true, 2)) << "," << Source(GetterSelections()) << "," << Source(GetterSelections(2, false, false)) << "," << Source(GetterSelections(2, false, true, true)) << "]\n";
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
