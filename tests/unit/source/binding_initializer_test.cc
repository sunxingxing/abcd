#include "../../../src/source/function_values.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/binding_initializer.h"
#include "../../../src/source/loop_syntax.h"
#include "../../../src/source/nullish_syntax.h"
#include "abcd/source/tree_printer.h"
#include "../../support/analysis/ssa_fixture.h"
#include <array>
#include <sstream>
#include <nlohmann/json.hpp>
using namespace abcd;
using namespace abcd::source;
namespace {
struct Fixture {
  FunctionTree tree{}; StmtId declaration, store;
  ExprId Expr(TreeExpression node) {
    ExprId id{static_cast<std::uint32_t>(tree.expressions.size())}; tree.expressions.push_back({std::move(node), {0}}); return id;
  }
  StmtId Stmt(TreeStatement node) {
    StmtId id{static_cast<std::uint32_t>(tree.statements.size())}; tree.statements.push_back({std::move(node), {0}}); return id;
  }
  ExprId Read(unsigned symbol) { return Expr(SymbolExpr{{symbol}}); }
  ExprId Call(unsigned symbol, std::optional<unsigned> argument = {}) {
    auto first = static_cast<std::uint32_t>(tree.arguments.size()); if (argument) tree.arguments.push_back(Read(*argument));
    return Expr(CallExpr{Read(symbol), {first, argument ? 1U : 0U}, CallReference::kValue});
  }
  StmtId Store(ExprId value) { return Stmt(ExpressionStmt{Expr(AssignmentExpr{SymbolTarget{{2}}, value})}); }
  void Reverse() {
    const auto expression = [&](ExprId id) { return ExprId{static_cast<std::uint32_t>(tree.expressions.size()-1-id.value)}; };
    for (auto& node : tree.expressions) std::visit([&](auto& syntax) {
      using T = std::decay_t<decltype(syntax)>;
      if constexpr (std::is_same_v<T, AssignmentExpr>) syntax.value = expression(syntax.value);
      else if constexpr (std::is_same_v<T, CallExpr>) syntax.callee = expression(syntax.callee);
      else if constexpr (std::is_same_v<T, AwaitExpr> || std::is_same_v<T, YieldExpr>) syntax.operand = expression(syntax.operand);
      else test::Check(std::holds_alternative<SymbolExpr>(node.expression));
    }, node.expression);
    for (auto& argument : tree.arguments) argument = expression(argument);
    for (auto& node : tree.statements) std::visit([&](auto& syntax) {
      using T = std::decay_t<decltype(syntax)>;
      if constexpr (std::is_same_v<T, ExpressionStmt>) syntax.expression = expression(syntax.expression);
      else if constexpr (std::is_same_v<T, ReturnStmt>) { if (syntax.value) syntax.value = expression(*syntax.value); }
      else if constexpr (std::is_same_v<T, IfStmt>) syntax.condition = expression(syntax.condition);
    }, node.statement);
    std::reverse(tree.expressions.begin(), tree.expressions.end());
    const auto statement = [&](StmtId id) { return StmtId{static_cast<std::uint32_t>(tree.statements.size()-1-id.value)}; };
    for (auto& child : tree.children) child = statement(child);
    for (auto& node : tree.statements) if (auto* syntax = std::get_if<IfStmt>(&node.statement)) syntax->consequent = statement(syntax->consequent);
    for (auto& anchor : tree.anchors) std::get<BlockBoundary>(anchor.position).block = statement(std::get<BlockBoundary>(anchor.position).block);
    tree.body = statement(tree.body); declaration = statement(declaration); store = statement(store);
    std::reverse(tree.statements.begin(), tree.statements.end());
  }
  Fixture(unsigned form, unsigned count) {
    tree.callable = form == 5 ? CallableKind::kAsyncFunction : form == 6 ? CallableKind::kGenerator : CallableKind::kFunction;
    tree.strictness = Strictness::kStrict; tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {0};
    tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
    tree.names = {u"seed", u"report", u"saved"}; tree.origins = {OriginalSite{{0}, {0}}};
    for (unsigned i = 0; i < 2; ++i) {
      tree.symbols.push_back({{0}, {i}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}});
      tree.parameters.push_back({{i}, {}, false});
    }
    tree.symbols.push_back({{1}, {2}, form == 7 ? BindingKind::kLocal : BindingKind::kCompilerTemporary,
        DeclarationKind::kLet, NameEvidence::kSynthesized, form == 7 ? std::optional<TemporaryReason>{} : TemporaryReason::kMultipleUses});
    tree.statements.push_back({BlockStmt{{1}, 0, 0}, {0}});
    declaration = Stmt(DeclarationStmt{{2}, {}});
    std::vector<StmtId> body{declaration};
    if (form == 2) body.push_back(Stmt(ExpressionStmt{Call(1, 2)}));
    ExprId value = form == 3 ? Read(2) : Call(0);
    if (form == 5) value = Expr(AwaitExpr{value}); if (form == 6) value = Expr(YieldExpr{value});
    store = Store(value);
    if (form == 4) {
      tree.scopes.push_back({ScopeKind::kBlock, ScopeId{1}});
      const auto first = static_cast<std::uint32_t>(tree.children.size()); tree.children.push_back(store);
      const auto block = Stmt(BlockStmt{{2}, first, 1});
      body.push_back(Stmt(IfStmt{Read(0), block, {}}));
    } else body.push_back(store);
    for (unsigned i = 0; i < count; ++i) body.push_back(Stmt(ExpressionStmt{Call(1, 2)}));
    if (form == 1) body.push_back(Store(Call(0)));
    body.push_back(Stmt(ReturnStmt{Read(2)}));
    const auto first = static_cast<std::uint32_t>(tree.children.size()); tree.children.insert(tree.children.end(), body.begin(), body.end());
    std::get<BlockStmt>(tree.statements[0].statement) = {{1}, first, static_cast<std::uint32_t>(body.size()), {}};
    for (unsigned i = 0; i <= body.size(); ++i) {
      tree.anchors.push_back({BlockBoundary{{0}, i}, {0}});
      tree.operations.push_back({{i}, AnchorId{i}});
    }
  }
};
void Decoded(abc::ProfileId profile, unsigned copies, bool split, bool branch, bool emit) {
  using namespace analysis;
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kLDA_V8),2,op(abc::Opcode::kCALLARG0_IMM8),0,op(abc::Opcode::kSTA_V8),0};
  for (unsigned i = 0; i < copies; ++i) bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8),1,0,op(abc::Opcode::kMOV_V8_V8),0,1});
  if (split) bytes.insert(bytes.end(), {op(abc::Opcode::kJMP_IMM8),2});
  if (branch) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8),4,op(abc::Opcode::kJEQZ_IMM8),7});
  for (unsigned i = 0; i < 2; ++i) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8),3,op(abc::Opcode::kCALLARG1_IMM8_V8),0,0});
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8),0,op(abc::Opcode::kRETURN)});
  const auto body = test::Body(bytes);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(2, branch ? 3 : 2, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {200}, {100}, body, frame, InputCaptureMode::kConditional));
  const auto graph = std::get<FunctionValues>(BuildFunctionValues(input, body, frame)).graph;
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = {200};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"seed", u"report"}; syntax.parameters = {
      {NameId{0}, NameEvidence::kDebugRange}, {NameId{1}, NameEvidence::kDebugRange}};
  if (branch) { syntax.names.push_back(u"flag"); syntax.parameters.push_back({NameId{2}, NameEvidence::kDebugRange}); }
  std::vector<ExpressionConstant> constants(graph.ops.size()); const ExpressionConstants resolved{constants, syntax.names.size()};
  const auto built = BuildFunctionTree(graph, control, evaluation, syntax, resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built)); const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  std::ostringstream output; test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  if (branch) test::Check(output.str().find("const ") != std::string::npos);
  if (emit) std::cout << nlohmann::json{{"source", output.str()}, {"decoded", true}, {"branch", branch}} << '\n';
}
}
int main(int argc, char**) {
  std::array<std::uint64_t, 8> small{}, large{};
  for (unsigned form = 0; form < 8; ++form) for (unsigned count : {0U, 1U, 200U, 201U, 400U, 800U}) for (bool reverse : {false,true}) {
    Fixture fixture(form, count); std::ostringstream output;
    if (reverse) fixture.Reverse();
    const auto loops = std::get<LoopSyntax>(PlanLoopSyntax(fixture.tree));
    const auto nullish = std::get<NullishSyntax>(PlanNullishSyntax(fixture.tree));
    const auto planned = PlanBindingInitializers(fixture.tree, loops, nullish);
    test::Check(std::holds_alternative<BindingInitializers>(planned));
    if (!reverse && count == 400) small[form] = std::get<BindingInitializers>(planned).work;
    if (!reverse && count == 800) large[form] = std::get<BindingInitializers>(planned).work;
    test::Check(std::get<TreeStructureError>(PlanBindingInitializers(fixture.tree, loops, nullish, {0})) == TreeStructureError::kResourceLimit);
    const auto printed = PrintFunctionTree(fixture.tree, output);
    test::Check(std::holds_alternative<PrintedFunction>(printed));
    const auto& ranges = std::get<PrintedFunction>(printed);
    const bool fold = form == 0 || form == 1 || form == 5 || form == 6;
    test::Check((ranges.statements[fixture.declaration.value].begin == ranges.statements[fixture.declaration.value].end) == fold);
    if (fold) test::Check(output.str().find(form == 1 ? "let saved = " : "const saved = ") != std::string::npos);
    for (const auto& at : ranges.expressions) test::Check(at.begin < at.end && at.end <= output.str().size());
    for (auto at : ranges.anchors) test::Check(at > 0 && at <= output.str().size());
    if (argc > 1) std::cout << nlohmann::json{{"source", output.str()}, {"form", form}, {"count", count}, {"fold", fold}, {"decoded", false}} << '\n';
  }
  for (unsigned i = 0; i < small.size(); ++i) test::Check(large[i] < small[i] * 3);
  for (auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (unsigned copies : {0U, 1U, 200U, 201U}) for (bool split : {false, true}) for (bool branch : {false, true})
      Decoded(profile, copies, split, branch, argc > 1);
}
