#include "../../../src/source/loop_syntax.h"
#include "../../../src/source/function_values.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_structure.h"
#include "abcd/source/tree_printer.h"
#include "../../support/analysis/ssa_fixture.h"
#include <algorithm>
#include <sstream>
#include <nlohmann/json.hpp>
using namespace abcd;
using namespace abcd::source;
using test::Check;
namespace {
struct Syntax {
  FunctionTree tree{};
  StmtId loop{1};
  Syntax() {
    tree.owner = {0}; tree.callable = CallableKind::kFunction; tree.strictness = Strictness::kStrict;
    tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {0};
    tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
    tree.origins = {OriginalSite{{0}, {0}}};
    tree.names = {u"prefix", u"condition", u"exit", u"body", u"choice", u"local", u"caught"};
    for (std::uint32_t i = 0; i < 5; ++i) {
      tree.symbols.push_back({{0}, {i}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}});
      tree.parameters.push_back({{i}, {}, false});
    }
    tree.statements = {{BlockStmt{{1}, 0, 0, {}}, {0}}, {LoopStmt{{0}, {}}, {0}}};
  }
  ExprId Add(TreeExpression expression) {
    const ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};
    tree.expressions.push_back({std::move(expression), {0}}); return id;
  }
  ExprId Call(std::uint32_t symbol) { return Add(CallExpr{Add(SymbolExpr{{symbol}}), {0, 0}, CallReference::kValue}); }
  StmtId Statement(TreeStatement statement) {
    const StmtId id{static_cast<std::uint32_t>(tree.statements.size())}; tree.statements.push_back({std::move(statement), {0}}); return id;
  }
  ScopeId Scope(ScopeId parent) {
    const ScopeId id{static_cast<std::uint32_t>(tree.scopes.size())}; tree.scopes.push_back({ScopeKind::kBlock, parent}); return id;
  }
  StmtId Block(ScopeId scope, std::vector<StmtId> children) {
    const auto first = static_cast<std::uint32_t>(tree.children.size());
    tree.children.insert(tree.children.end(), children.begin(), children.end());
    return Statement(BlockStmt{scope, first, static_cast<std::uint32_t>(children.size()), {}});
  }
  void Build(unsigned form, unsigned count) {
    const auto scope = Scope({1});
    const auto exit_scope = Scope(scope), body_scope = Scope(scope);
    StmtId exit;
    if (form == 3) {
      const auto first = Block(Scope(exit_scope), {Statement(BreakStmt{loop, {}})});
      const auto second = Block(Scope(exit_scope), {});
      exit = Block(exit_scope, {Statement(IfStmt{Call(4), first, second}),
          Statement(ExpressionStmt{Call(2)}), Statement(BreakStmt{loop, {}})});
    } else if (form == 2) exit = Block(exit_scope, {Statement(ExpressionStmt{Call(2)}), Statement(BreakStmt{loop, {}})});
    else exit = Block(exit_scope, {Statement(BreakStmt{loop, {}})});
    StmtId body;
    if (form == 5) {
      const auto protected_body = Block(Scope(body_scope), {Statement(ExpressionStmt{Call(3)}), Statement(ContinueStmt{loop, {}})});
      const auto finalizer = Block(Scope(body_scope), {Statement(ExpressionStmt{Call(2)})});
      body = Block(body_scope, {Statement(TryStmt{protected_body, {}, finalizer})});
    } else body = Block(body_scope, {Statement(ExpressionStmt{Call(3)}), Statement(ContinueStmt{loop, {}})});
    std::optional<SymbolId> local;
    if (form == 4) {
      local = SymbolId{static_cast<std::uint32_t>(tree.symbols.size())};
      tree.symbols.push_back({scope, {5}, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kSynthesized, {}});
    }
    auto input = Call(1);
    if (local) input = Add(SequenceExpr{input, Add(SymbolExpr{*local})});
    if (form == 6) { tree.callable = CallableKind::kAsyncFunction; input = Add(AwaitExpr{input}); }
    if (form == 7) { tree.callable = CallableKind::kGenerator; input = Add(YieldExpr{input}); }
    const auto condition = Add(UnaryExpr{TreeUnary::kNot, input});
    const auto guard = Statement(IfStmt{condition, exit, form == 1 || form == 4 ? std::optional<StmtId>{} : body});
    std::vector<StmtId> children;
    for (unsigned i = 0; i < count; ++i) {
      auto prefix = Statement(ExpressionStmt{Call(0)});
      if (i % 2) prefix = Block(Scope(scope), {prefix});
      children.push_back(prefix);
    }
    children.push_back(guard);
    if (form == 1 || form == 4) children.push_back(body);
    if (local) children.insert(children.end() - 1, Statement(DeclarationStmt{*local, {}}));
    const auto block = Block(scope, children); std::get<LoopStmt>(tree.statements[loop.value].statement).body = block;
    const auto first = static_cast<std::uint32_t>(tree.children.size()); tree.children.push_back(loop);
    std::get<BlockStmt>(tree.statements[0].statement) = {{1}, first, 1, {}};
  }
  void Reverse() {
    const auto count = static_cast<std::uint32_t>(tree.statements.size());
    const auto remap = [&](StmtId id) { return StmtId{count - 1 - id.value}; };
    for (auto& child : tree.children) child = remap(child);
    for (auto& statement : tree.statements) {
      auto& node = statement.statement;
      if (auto* loop_node = std::get_if<LoopStmt>(&node)) loop_node->body = remap(loop_node->body);
      if (auto* branch = std::get_if<IfStmt>(&node)) { branch->consequent = remap(branch->consequent); if (branch->alternate) branch->alternate = remap(*branch->alternate); }
      if (auto* region = std::get_if<TryStmt>(&node)) {
        region->body = remap(region->body); if (region->handler) region->handler->body = remap(region->handler->body);
        if (region->finalizer) region->finalizer = remap(*region->finalizer);
      }
      if (auto* transfer = std::get_if<BreakStmt>(&node)) transfer->target = remap(transfer->target);
      if (auto* transfer = std::get_if<ContinueStmt>(&node)) transfer->target = remap(transfer->target);
    }
    tree.body = remap(tree.body); loop = remap(loop); std::reverse(tree.statements.begin(), tree.statements.end());
  }
};
std::string Decoded(abc::ProfileId profile, std::uint32_t locals, unsigned copies, bool split) {
  const auto op = [](abc::Opcode value) { return static_cast<std::uint8_t>(value); };
  std::vector<std::uint8_t> bytes;
  const auto emit = [&](abc::Opcode value, std::uint8_t operand) { bytes.push_back(op(value)); bytes.push_back(operand); };
  const auto boundary = [&] { if (split) emit(abc::Opcode::kJMP_IMM8, 2); };
  emit(abc::Opcode::kLDA_V8, locals); emit(abc::Opcode::kCALLARG0_IMM8, 0); boundary();
  emit(abc::Opcode::kLDA_V8, locals + 1);
  for (unsigned i = 0; i < copies; ++i) {
    emit(abc::Opcode::kSTA_V8, locals - 1); boundary(); emit(abc::Opcode::kLDA_V8, locals - 1);
  }
  emit(abc::Opcode::kCALLARG0_IMM8, 0);
  for (unsigned i = 0; i < copies; ++i) {
    emit(abc::Opcode::kSTA_V8, 0); boundary(); emit(abc::Opcode::kLDA_V8, 0);
  }
  const auto branch = bytes.size(); emit(abc::Opcode::kJEQZ_IMM8, 0);
  emit(abc::Opcode::kLDA_V8, locals + 2); emit(abc::Opcode::kCALLARG0_IMM8, 0); boundary();
  const auto back = bytes.size(); Check(back <= static_cast<std::size_t>(std::numeric_limits<std::int8_t>::max()));
  emit(abc::Opcode::kJMP_IMM8, static_cast<std::uint8_t>(-static_cast<int>(back)));
  bytes[branch + 1] = static_cast<std::uint8_t>(bytes.size() - branch); bytes.push_back(op(abc::Opcode::kRETURNUNDEFINED));
  const auto body = test::Body(bytes);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(locals, 3, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto control = std::get<ControlPlan>(BuildControlPlan(values.graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(values.graph));
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = {300};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.texts = {u"number"}; syntax.parameters.resize(frame.parameters);
  const std::vector<ExpressionConstant> constants(values.graph.ops.size());
  const ExpressionConstants resolved{constants, 0, 1, TextId{0}};
  const auto built = BuildFunctionTree(values.graph, control, evaluation, syntax, resolved);
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  Check(tree.recovery_issues.empty());
  Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(values.graph, control, evaluation, tree, resolved)));
  const auto plan = std::get<LoopSyntax>(PlanLoopSyntax(tree));
  Check(std::count_if(plan.conditions.begin(), plan.conditions.end(), [](const auto& condition) { return bool(condition); }) == 1);
  std::ostringstream source; Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, source))); return source.str();
}

}
int main(int argc, char**) {
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const std::uint32_t locals : {1U, 7U}) for (unsigned copies : {0U, 1U, 4U}) for (bool split : {false, true}) {
      const auto source = Decoded(profile, locals, copies, split);
      if (argc > 1) std::cout << nlohmann::json{{"form", 0}, {"count", 1}, {"decoded", true}, {"source", source}} << '\n';
    }
  std::uint64_t work_400 = 0;
  for (unsigned form = 0; form < 8; ++form) for (unsigned count : {0, 1, 200, 201, 400, 800}) for (bool reversed : {false, true}) {
    Syntax syntax; syntax.Build(form, count); if (reversed) syntax.Reverse();
    const auto structure = VerifyTreeStructure(syntax.tree);
    Check(std::holds_alternative<TreeStructure>(structure));
    const auto planned = PlanLoopSyntax(syntax.tree); Check(std::holds_alternative<LoopSyntax>(planned));
    const auto& plan = std::get<LoopSyntax>(planned);
    Check(bool(plan.conditions[syntax.loop.value]) == (form != 4));
    Check(std::get<TreeStructureError>(PlanLoopSyntax(syntax.tree, {0, analysis::kDefaultSemanticWork})) == TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PlanLoopSyntax(syntax.tree, {analysis::SemanticBudget{}.bytes, 0})) == TreeStructureError::kWorkLimit);
    if (form == 0 && !reversed) {
      if (count == 400) work_400 = plan.work;
      if (count == 800) Check(work_400 && plan.work < 3 * work_400);
    }
    std::ostringstream source; const auto printed = PrintFunctionTree(syntax.tree, source);
    Check(std::holds_alternative<PrintedFunction>(printed));
    const auto& positions = std::get<PrintedFunction>(printed);
    for (const auto& range : positions.expressions) Check(range.begin < range.end && range.end <= source.str().size());
    for (const auto& range : positions.statements) Check(range.begin <= range.end && range.end <= source.str().size());
    if (argc > 1) std::cout << nlohmann::json{{"form", form}, {"count", count}, {"reversed", reversed}, {"source", source.str()}, {"work", plan.work}} << '\n';
  }
}
