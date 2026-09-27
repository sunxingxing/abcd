#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_structure.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/function_abi.h"
#include "../../../src/source/tree_globals.h"

#include <cstdlib>
#include <type_traits>
#include <utility>

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
    return Add(block, order, pc, operation, result, std::span(operands.begin(), operands.size()), effects);
  }
  analysis::OpId Add(std::uint32_t block, std::uint32_t order, std::uint32_t pc,
      analysis::ValueOperation operation, bool result,
      std::span<const std::pair<analysis::OperandRole, analysis::ValueOperand>> operands,
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
        std::holds_alternative<analysis::ValueGlobalRead>(operation) ||
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
  std::variant<TreeSemantics, TreeSemanticError> Verify(
      const FunctionTree& tree, analysis::SemanticBudget budget = {}) const {
    auto control = BuildControlPlan(graph);
    auto evaluation = BuildEvaluationPlan(graph);
    Check(std::holds_alternative<ControlPlan>(control));
    Check(std::holds_alternative<EvaluationPlan>(evaluation));
    return VerifyTreeSemantics(graph, std::get<ControlPlan>(control),
        std::get<EvaluationPlan>(evaluation), tree, {constants, 2, 0, {}}, budget);
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
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
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
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 4, abc::DecodeCallFlags({})));
  const auto census = BuildFunctionAbi(f.graph, tree, frame, 1);
  Check(std::holds_alternative<ConstructedAbi>(census));
  const auto& abi = std::get<ConstructedAbi>(census);
  Check(abi.abi.parameters.size() == 1 && abi.abi.length == 1);
  Check(abi.abi.parameters[0].reads.operands == 2);
  Check(abi.abi.parameters[0].reads.references == 2);
  Check(abi.abi.parameters[0].writes == 0);
  Check(abi.abi.receiver.operands == 1 && abi.abi.receiver.references == 1);
  Check(std::get<FunctionAbiError>(BuildFunctionAbi(f.graph, tree, frame, 2)) == FunctionAbiError::kLength);
  // Census is not a source-publication proof: these mutations exercise only
  // signature shape/length. Default body correspondence is verified separately.
  auto default_tree = tree;
  const ExprId initializer{static_cast<std::uint32_t>(default_tree.expressions.size())};
  default_tree.expressions.push_back({TreeLiteral{UndefinedLiteral{}}, {0}});
  default_tree.parameters[0].initializer = initializer;
  const auto default_census = BuildFunctionAbi(f.graph, default_tree, frame, 0);
  Check(std::holds_alternative<ConstructedAbi>(default_census));
  Check(std::get<ConstructedAbi>(default_census).abi.layout == ParameterLayout::kLiteralDefaults);
  Check(std::get<ConstructedAbi>(default_census).abi.length == 0);
  Check(std::get<FunctionAbiError>(BuildFunctionAbi(f.graph, default_tree, frame, 1)) == FunctionAbiError::kLength);
  default_tree.expressions.back().expression = SymbolExpr{default_tree.parameters[0].symbol};
  Check(std::get<FunctionAbiError>(BuildFunctionAbi(f.graph, default_tree, frame, 0)) == FunctionAbiError::kParameter);
  default_tree.expressions.back().expression = TreeLiteral{UndefinedLiteral{}};
  default_tree.parameters[0].rest = true;
  Check(std::get<FunctionAbiError>(BuildFunctionAbi(f.graph, default_tree, frame, 0)) == FunctionAbiError::kParameter);
  auto changed_graph = f.graph;
  ++changed_graph.revision.value;
  Check(std::get<FunctionAbiError>(BuildFunctionAbi(changed_graph, tree, frame, 1)) == FunctionAbiError::kRevision);
  changed_graph = f.graph;
  changed_graph.values[0].origin = analysis::EntryDefinition{analysis::CallableEntry{}};
  Check(std::get<FunctionAbiError>(BuildFunctionAbi(changed_graph, tree, frame, 1)) == FunctionAbiError::kEntry);
  auto absent_receiver = frame;
  absent_receiver.this_value.reset();
  Check(std::get<FunctionAbiError>(BuildFunctionAbi(f.graph, tree, absent_receiver, 1)) == FunctionAbiError::kReceiver);
  analysis::SemanticBudget census_budget;
  census_budget.work = abi.work - 1;
  Check(std::get<FunctionAbiError>(BuildFunctionAbi(f.graph, tree, frame, 1, census_budget)) == FunctionAbiError::kWorkLimit);
  ++census_budget.work;
  Check(std::holds_alternative<ConstructedAbi>(BuildFunctionAbi(f.graph, tree, frame, 1, census_budget)));
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
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
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
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
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
void SemanticMutations() {
  auto fixture = BranchFixture();
  const auto built = fixture.Build();
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto original = std::get<ConstructedFunction>(built).tree;
  const auto checked = fixture.Verify(original);
  Check(std::holds_alternative<TreeSemantics>(checked));
  const auto& root = std::get<BlockStmt>(original.statements[original.body.value].statement);
  const auto branch_id = original.children[root.first_child];
  const auto& branch = std::get<IfStmt>(original.statements[branch_id.value].statement);
  const auto& alternative = std::get<BlockStmt>(original.statements[branch.alternate->value].statement);
  const auto write_id = original.children[alternative.first_child];
  const auto assignment_id = std::get<ExpressionStmt>(original.statements[write_id.value].statement).expression;
  const auto return_id = original.children[root.first_child + 1];
  const auto return_value = *std::get<ReturnStmt>(original.statements[return_id.value].statement).value;

  auto tree = original;
  auto& reversed = std::get<IfStmt>(tree.statements[branch_id.value].statement);
  const auto former = reversed.consequent;
  reversed.consequent = *reversed.alternate;
  reversed.alternate = former;
  Check(std::get<TreeSemanticError>(fixture.Verify(tree)) == TreeSemanticError::kFlow);

  tree = original;
  std::get<BinaryExpr>(tree.expressions[branch.condition.value].expression).operation = analysis::BinaryOperator::kStrictNotEqual;
  Check(std::get<TreeSemanticError>(fixture.Verify(tree)) == TreeSemanticError::kOperator);

  tree = original;
  auto& assigned = std::get<AssignmentExpr>(tree.expressions[assignment_id.value].expression);
  std::swap(std::get<MemberTarget>(assigned.target).base, assigned.value);
  Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(tree)));

  tree = original;
  std::get<MemberTarget>(std::get<AssignmentExpr>(tree.expressions[assignment_id.value].expression).target).key = NameId{0};
  Check(std::get<TreeSemanticError>(fixture.Verify(tree)) == TreeSemanticError::kConstant);

  tree = original;
  tree.expressions[return_value.value].expression = TreeLiteral{BooleanLiteral{false}};
  Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(tree)));

  tree = original;
  std::get<OriginalSite>(tree.origins[4]).pc.value = 41;
  Check(std::get<TreeSemanticError>(fixture.Verify(tree)) == TreeSemanticError::kOrigin);

  tree = original;
  tree.origin_parts.back() = {4};
  Check(std::get<TreeSemanticError>(fixture.Verify(tree)) == TreeSemanticError::kOrigin);

  tree = original;
  ++tree.value_revision.value;
  Check(std::get<TreeSemanticError>(fixture.Verify(tree)) == TreeSemanticError::kFlow);

  analysis::SemanticBudget budget;
  budget.work = std::get<TreeSemantics>(checked).work - 1;
  Check(std::get<TreeSemanticError>(fixture.Verify(original, budget)) == TreeSemanticError::kWorkLimit);
  ++budget.work;
  Check(std::holds_alternative<TreeSemantics>(fixture.Verify(original, budget)));
}

enum class CallCase { kValue, kMember, kConstruct };
void GlobalTree() {
  using namespace analysis;
  Fixture fixture;
  fixture.Add(0, 0, 31, ValueGlobalRead{GlobalLookup::kName, {0}}, true, {}, LanguageEffect::kGlobalAccess);
  fixture.Add(0, 1, 35, ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{2}}});
  fixture.graph.blocks = {{{0}, 2, {0}, 0}};
  auto built = fixture.Build();
  Check(std::holds_alternative<ConstructedFunction>(built));
  auto tree = std::get<ConstructedFunction>(std::move(built)).tree;
  Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
  const GlobalScope scope{};
  Check(std::holds_alternative<TreeGlobals>(VerifyTreeGlobals(tree, &scope)));
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, nullptr)) == TreeGlobalError::kMissingContext);
  bool changed = false;
  for (auto& node : tree.expressions) {
    if (auto* global = std::get_if<GlobalNameExpr>(&node.expression)) {
      global->name = {0}; changed = true;
    }
  }
  Check(changed);
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope)) == TreeGlobalError::kShadowed);
  Check(std::get<TreeSemanticError>(fixture.Verify(tree)) == TreeSemanticError::kConstant);
  std::get<ValueGlobalRead>(fixture.graph.ops[0].operation).lookup = GlobalLookup::kVar;
  Check(std::get<FunctionTreeError>(fixture.Build()) == FunctionTreeError::kExpression);
}
void CallTree(CallCase kind, std::uint32_t count, bool computed) {
  using namespace analysis;
  Fixture fixture;
  if (computed)
    fixture.Add(0, 0, 0, ValuePropertyRead{}, true,
        {{OperandRole::kBase, ValueId{1}}, {OperandRole::kKey, ValueId{0}}}, kRead);
  else fixture.Add(0, 0, 0, ValueNamedRead{{0}}, true, {{OperandRole::kBase, ValueId{1}}}, kRead);
  std::vector<std::pair<OperandRole, ValueOperand>> inputs;
  inputs.emplace_back(kind == CallCase::kConstruct ? OperandRole::kConstructor : OperandRole::kCallee, ValueId{2});
  if (kind == CallCase::kMember) inputs.emplace_back(OperandRole::kReceiver, ValueId{1});
  for (std::uint32_t i = 0; i < count; ++i)
    inputs.emplace_back(OperandRole::kArgument, analysis::IntegerLiteral{static_cast<std::int32_t>(i)});
  const ValueOperation operation = kind == CallCase::kConstruct ? ValueOperation{ValueConstruct{count}} :
      ValueOperation{ValueInvoke{kind == CallCase::kMember ? InvokeReceiver::kExplicit : InvokeReceiver::kUndefined, count}};
  fixture.Add(0, 1, 4, operation, true, inputs, LanguageEffect::kUserCode);
  fixture.Add(0, 2, 8, ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{3}}});
  fixture.graph.blocks = {{{0}, 3, {0}, 0}};
  const auto built = fixture.Build();
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  Check(tree.arguments.size() == count);
  Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
  std::optional<ExprId> call;
  for (std::uint32_t i = 0; i < tree.expressions.size(); ++i)
    if (std::holds_alternative<CallExpr>(tree.expressions[i].expression) ||
        std::holds_alternative<ConstructExpr>(tree.expressions[i].expression)) call = ExprId{i};
  Check(call.has_value());
  if (kind != CallCase::kConstruct) {
    auto forged = tree;
    auto& node = std::get<CallExpr>(forged.expressions[call->value].expression);
    node.reference = kind == CallCase::kMember ? CallReference::kValue : CallReference::kMember;
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(forged)));
    Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(forged)));
  }
  if (count >= 2) {
    auto forged = tree;
    std::swap(forged.arguments[0], forged.arguments[1]);
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(forged)));
    Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(forged)));
    forged = tree;
    forged.arguments[1] = forged.arguments[0];
    Check(std::get<TreeStructureError>(VerifyTreeStructure(forged)) == TreeStructureError::kOwnership);
  }
  auto orphan = tree;
  orphan.arguments.push_back(*call);
  Check(std::get<TreeStructureError>(VerifyTreeStructure(orphan)) == TreeStructureError::kOwnership);
  auto invalid = tree;
  std::visit([&](auto& node) {
    using T = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<T, CallExpr> || std::is_same_v<T, ConstructExpr>) ++node.arguments.count;
  }, invalid.expressions[call->value].expression);
  Check(std::get<TreeStructureError>(VerifyTreeStructure(invalid)) == TreeStructureError::kRange);
}
void LoopTree() {
  auto fixture = BranchFixture();
  // The body retains the two effectful property operations, then takes the
  // original edge back to the header. The header read is evaluated each time.
  fixture.graph.edges[2].target = {0};
  const auto built = fixture.Build();
  const auto* constructed = std::get_if<ConstructedFunction>(&built);
  Check(constructed != nullptr);
  const auto& tree = constructed->tree;
  Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
  std::size_t loops = 0, breaks = 0, continues = 0;
  for (std::size_t i = 0; i < tree.statements.size(); ++i) {
    const auto& statement = tree.statements[i];
    loops += std::holds_alternative<LoopStmt>(statement.statement);
    breaks += std::holds_alternative<BreakStmt>(statement.statement);
    continues += std::holds_alternative<ContinueStmt>(statement.statement);
    if (const auto* transfer = std::get_if<ContinueStmt>(&statement.statement)) {
      auto changed = tree;
      changed.statements[i].statement = BreakStmt{transfer->target};
      Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(changed)));
      Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(changed)));
      changed = tree;
      changed.origins[statement.origin.value] = EdgeOrigin{{0}};
      Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(changed)));
    }
  }
  Check(loops == 1 && breaks == 1 && continues == 1);
  auto changed = tree;
  for (auto& origin : changed.origins)
    if (auto* loop = std::get_if<LoopOrigin>(&origin)) loop->header = {1};
  Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(changed)));
}
void FreshAggregates() {
  using namespace analysis;
  for (const bool array : {false, true}) {
    Fixture fixture;
    fixture.Add(0, 0, 0, array ? ValueOperation{ValueArrayAllocation{}} :
        ValueOperation{ValueObjectAllocation{}}, true, {}, LanguageEffect::kAllocation);
    if (array) {
      fixture.graph.ops[0].before_throw = LanguageEffect::kNone;
      fixture.graph.ops[0].exceptional = ExceptionalCompletion::kImpossible;
    }
    // Two references must observe the very same allocation, not two literals.
    fixture.Add(0, 1, 2, ValueNamedWrite{{0}}, false,
        {{OperandRole::kBase, ValueId{0}}, {OperandRole::kStoredValue, ValueId{2}}}, kWrite);
    fixture.Add(0, 2, 5, ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{2}}});
    fixture.graph.blocks = {{{0}, 3, {0}, 0}};
    const auto built = fixture.Build();
    const auto* result = std::get_if<ConstructedFunction>(&built);
    Check(result && std::holds_alternative<TreeSemantics>(fixture.Verify(result->tree)));
    std::size_t allocations = 0;
    for (std::size_t i = 0; i < result->tree.expressions.size(); ++i) {
      const auto& expression = result->tree.expressions[i].expression;
      if (!std::holds_alternative<EmptyArrayExpr>(expression) &&
          !std::holds_alternative<EmptyObjectExpr>(expression)) continue;
      ++allocations;
      auto changed = result->tree;
      changed.expressions[i].expression = array ? TreeExpression{EmptyObjectExpr{}} : TreeExpression{EmptyArrayExpr{}};
      Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(changed)));
      Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(changed)));
    }
    Check(allocations == 1);
    const auto control = std::get<ControlPlan>(BuildControlPlan(fixture.graph));
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(fixture.graph));
    const auto flow = std::get<TreeFlow>(VerifyTreeFlow(fixture.graph, control, evaluation, result->tree));
    Check(flow.snapshots[0].has_value());
  }
}
void OwnDefinitionRefusal() {
  using namespace analysis;
  for (const auto kind : {OwnWriteKind::kStore, OwnWriteKind::kDefineField}) {
    Fixture fixture;
    constexpr std::uint16_t kCacheSlot = 0;
    fixture.Add(0, 0, 0, ValueOwnWrite{kind, {0}, kCacheSlot}, false,
        {{OperandRole::kBase, ValueId{0}}, {OperandRole::kStoredValue, ValueId{1}}}, kWrite);
    fixture.Add(0, 1, 5, ValueReturn{}, false,
        {{OperandRole::kReturnValue, SpecialValue::kUndefined}});
    fixture.graph.blocks = {{{0}, 2, {0}, 0}};
    // A typed imported definition cannot acquire assignment semantics through
    // the ordinary property writer without a separate descriptor/identity proof.
    Check(std::get<EvaluationError>(BuildEvaluationPlan(fixture.graph)) ==
        EvaluationError::kUnsupportedOperation);
  }
}
}  // namespace
int main() {
  BranchTree();
  SnapshotTree();
  NameAndConstantRefusals();
  PredicateSnapshot();
  SemanticMutations();
  GlobalTree();
  LoopTree();
  FreshAggregates();
  OwnDefinitionRefusal();
  for (const auto kind : {CallCase::kValue, CallCase::kMember, CallCase::kConstruct})
    for (const std::uint32_t count : {0U, 1U, 3U, 200U, 201U, 4096U})
      for (const bool computed : {false, true}) CallTree(kind, count, computed);
}
