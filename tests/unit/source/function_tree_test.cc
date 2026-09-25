#include "../../../src/source/function_tree.h"

#include <cstdlib>

namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool condition) { if (!condition) std::abort(); }
class Fixture {
 public:
  Fixture() {
    graph.values.push_back({analysis::EntryDefinition{analysis::ParameterEntry{0}},
                            analysis::ValueKinds::Language(), {}});
    graph.values.push_back({analysis::EntryDefinition{analysis::ReceiverEntry{}},
                            analysis::ValueKinds::Language(), {}});
  }
  analysis::OpId Add(std::uint32_t block, std::uint32_t order, std::uint32_t pc,
      analysis::ValueOperation operation, bool result,
      std::initializer_list<std::pair<analysis::OperandRole, analysis::ValueOperand>> operands,
      analysis::LanguageEffect effects = analysis::LanguageEffect::kNone) {
    const analysis::OpId id{static_cast<std::uint32_t>(graph.ops.size())};
    std::optional<analysis::ValueId> value;
    if (result) {
      value = analysis::ValueId{static_cast<std::uint32_t>(graph.values.size())};
      graph.values.push_back({analysis::OpResult{id}, analysis::ValueKinds::Language(), {}});
    }
    graph.origins.push_back({{id.value}, {pc}, {}});
    graph.ops.push_back({operation, {block}, order,
        {static_cast<std::uint32_t>(graph.uses.size())}, static_cast<std::uint32_t>(operands.size()),
        value, effects, effects, effects == analysis::LanguageEffect::kNone
            ? analysis::ExceptionalCompletion::kImpossible : analysis::ExceptionalCompletion::kMayThrow,
        {id.value}});
    for (const auto& [role, operand] : operands) {
      const analysis::UseId use{static_cast<std::uint32_t>(graph.uses.size())};
      analysis::OperandUse occurrence{id, role, analysis::ReadPurpose::kOperand, operand, {}, {}, {}, {}};
      if (const auto* input = std::get_if<analysis::ValueId>(&operand)) {
        auto& definition = graph.values[input->value];
        occurrence.next = definition.first_use;
        if (occurrence.next) graph.uses[occurrence.next->value].previous = use;
        definition.first_use = use;
      }
      graph.uses.push_back(occurrence);
    }
    if (std::holds_alternative<analysis::ValueNamedRead>(operation) ||
        std::holds_alternative<analysis::ValueNamedWrite>(operation))
      constants.emplace_back(NameId{1});
    else constants.emplace_back(std::monostate{});
    return id;
  }
  FunctionSyntax Syntax() const {
    FunctionSyntax syntax{};
    syntax.owner = {3};
    syntax.callable = CallableKind::kMethod;
    syntax.strictness = Strictness::kStrict;
    syntax.names = {u"params", u"pathStack"};
    syntax.parameters.push_back({NameId{0}, NameEvidence::kDebugRange});
    return syntax;
  }
  std::variant<ConstructedFunction, FunctionTreeError> Build(
      analysis::SemanticBudget budget = {}) const {
    auto control = BuildControlPlan(graph);
    auto evaluation = BuildEvaluationPlan(graph);
    Check(std::holds_alternative<ControlPlan>(control));
    Check(std::holds_alternative<EvaluationPlan>(evaluation));
    return BuildFunctionTree(graph, std::get<ControlPlan>(control),
        std::get<EvaluationPlan>(evaluation), Syntax(), {constants, 2, 0, {}}, budget);
  }
  analysis::ValueProgram graph;
  std::vector<ExpressionConstant> constants;
};
constexpr auto kRead = analysis::LanguageEffect::kHeapRead | analysis::LanguageEffect::kUserCode;
constexpr auto kWrite = analysis::LanguageEffect::kHeapWrite | analysis::LanguageEffect::kUserCode;

Fixture BranchFixture() {
  using namespace analysis;
  Fixture f;
  f.Add(0, 0, 14, ValueNamedRead{{0}}, true, {{OperandRole::kBase, ValueId{0}}}, kRead);
  f.Add(0, 1, 21, ValueBinary{BinaryOperator::kStrictEqual}, true,
        {{OperandRole::kLeft, ValueId{2}}, {OperandRole::kRight, SpecialValue::kUndefined}});
  f.Add(0, 2, 24, ValueBranch{UnaryOperator::kTrueOrNonzeroNumber}, false,
        {{OperandRole::kCondition, ValueId{3}}});
  f.Add(1, 0, 36, ValueNamedRead{{0}}, true, {{OperandRole::kBase, ValueId{0}}}, kRead);
  f.Add(1, 1, 40, ValueNamedWrite{{0}}, false,
        {{OperandRole::kBase, ValueId{1}}, {OperandRole::kStoredValue, ValueId{4}}}, kWrite);
  f.Add(1, 2, 44, ValueJump{}, false, {});
  f.Add(2, 0, 46, ValueReturn{}, false, {{OperandRole::kReturnValue, SpecialValue::kUndefined}});
  f.graph.blocks = {{{0}, 3, {0}, 2}, {{3}, 3, {2}, 1}, {{6}, 1, {3}, 0}};
  f.graph.edges = {{{0}, {2}, FlowEdgeKind::kTaken, 0},
                   {{0}, {1}, FlowEdgeKind::kFallthrough, 1},
                   {{1}, {2}, FlowEdgeKind::kTaken, 0}};
  return f;
}
void BranchTree() {
  auto f = BranchFixture();
  auto built = f.Build();
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  Check(tree.symbols.size() == 1 && tree.parameters.size() == 1);
  Check(tree.names[tree.symbols[0].name.value] == u"params");
  std::size_t reads = 0;
  for (const auto& node : tree.expressions)
    if (std::holds_alternative<MemberExpr>(node.expression)) ++reads;
  Check(reads == 2);
  const auto& root = std::get<BlockStmt>(tree.statements[tree.body.value].statement);
  Check(root.child_count == 2);
  const auto& branch = std::get<IfStmt>(tree.statements[tree.children[root.first_child].value].statement);
  Check(branch.alternate.has_value());
  const auto& taken = std::get<BlockStmt>(tree.statements[branch.consequent.value].statement);
  Check(taken.child_count == 0);
  const auto& body = std::get<BlockStmt>(tree.statements[branch.alternate->value].statement);
  Check(body.child_count == 1);
  const auto& write = std::get<ExpressionStmt>(tree.statements[tree.children[body.first_child].value].statement);
  Check(std::holds_alternative<AssignmentExpr>(tree.expressions[write.expression.value].expression));
  Check(std::get<OriginalSite>(tree.origins[tree.expressions[write.expression.value].origin.value]).pc.value == 40);
  Check(tree.anchors.size() == 1);
  const auto& anchor = std::get<BlockBoundary>(tree.anchors[0].position);
  Check(anchor.block == *branch.alternate && anchor.index == body.child_count);
  Check(tree.operations.size() == f.graph.ops.size());
  analysis::SemanticBudget limited;
  limited.work = std::get<ConstructedFunction>(built).work - 1;
  Check(std::get<FunctionTreeError>(f.Build(limited)) == FunctionTreeError::kWorkLimit);
  limited.work += 1;
  Check(std::holds_alternative<ConstructedFunction>(f.Build(limited)));
}
void SnapshotTree() {
  using namespace analysis;
  Fixture f;
  f.Add(0, 0, 0, ValueNamedRead{{0}}, true, {{OperandRole::kBase, ValueId{0}}}, kRead);
  f.Add(0, 1, 4, ValueBinary{BinaryOperator::kStrictEqual}, true,
        {{OperandRole::kLeft, ValueId{2}}, {OperandRole::kRight, ValueId{2}}});
  f.Add(0, 2, 8, ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{3}}});
  f.graph.blocks = {{{0}, 3, {0}, 0}};
  const auto built = f.Build();
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  Check(tree.symbols.size() == 2);
  Check(tree.symbols[1].temporary == TemporaryReason::kMultipleUses);
  const auto& root = std::get<BlockStmt>(tree.statements[tree.body.value].statement);
  Check(root.child_count == 3);
  const auto& declaration = std::get<DeclarationStmt>(tree.statements[tree.children[root.first_child].value].statement);
  Check(declaration.symbol == SymbolId{1} && !declaration.initializer);
  const auto& snapshot = std::get<ExpressionStmt>(tree.statements[tree.children[root.first_child + 1].value].statement);
  const auto& assignment = std::get<AssignmentExpr>(tree.expressions[snapshot.expression.value].expression);
  Check(std::get<SymbolTarget>(assignment.target).symbol == SymbolId{1});
}
void NameAndConstantRefusals() {
  auto f = BranchFixture();
  f.constants[0] = NameId{2};
  Check(std::get<FunctionTreeError>(f.Build()) == FunctionTreeError::kExpression);
  f.constants[0] = NameId{1};
  auto control = BuildControlPlan(f.graph);
  auto evaluation = BuildEvaluationPlan(f.graph);
  auto syntax = f.Syntax();
  syntax.names[0] = u"$value0";
  auto built = BuildFunctionTree(f.graph, std::get<ControlPlan>(control),
      std::get<EvaluationPlan>(evaluation), std::move(syntax), {f.constants, 2, 0, {}});
  Check(std::get<FunctionTreeError>(built) == FunctionTreeError::kName);
}
void PredicateSnapshot() {
  using namespace analysis;
  for (const auto predicate : {UnaryOperator::kFalseOrZero, UnaryOperator::kTrueOrNonzeroNumber}) {
    Fixture f;
    f.Add(0, 0, 0, ValueNamedRead{{0}}, true, {{OperandRole::kBase, ValueId{0}}}, kRead);
    f.Add(0, 1, 4, ValueUnary{predicate}, true, {{OperandRole::kInput, ValueId{2}}});
    f.Add(0, 2, 8, ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{3}}});
    f.graph.blocks = {{{0}, 3, {0}, 0}};
    f.graph.origins[0].original = abc::CodeOffset{91};
    auto control = BuildControlPlan(f.graph);
    auto evaluation = BuildEvaluationPlan(f.graph);
    auto syntax = f.Syntax();
    syntax.texts.push_back(u"number");
    const auto built = BuildFunctionTree(f.graph, std::get<ControlPlan>(control),
        std::get<EvaluationPlan>(evaluation), std::move(syntax), {f.constants, 2, 1, TextId{0}});
    Check(std::holds_alternative<ConstructedFunction>(built));
    const auto& tree = std::get<ConstructedFunction>(built).tree;
    Check(tree.symbols.size() == 2 && tree.symbols[1].temporary == TemporaryReason::kPredicateSnapshot);
    std::size_t reads = 0;
    std::size_t references = 0;
    for (const auto& node : tree.expressions) {
      if (std::holds_alternative<MemberExpr>(node.expression)) ++reads;
      if (const auto* symbol = std::get_if<SymbolExpr>(&node.expression))
        if (symbol->symbol == SymbolId{1}) ++references;
    }
    Check(reads == 1);
    Check(references == (predicate == UnaryOperator::kFalseOrZero ? 2U : 3U));
    Check(std::get<OriginalSite>(tree.origins[0]).original == abc::CodeOffset{91});
  }
}
}  // namespace
int main() {
  BranchTree();
  SnapshotTree();
  NameAndConstantRefusals();
  PredicateSnapshot();
}
