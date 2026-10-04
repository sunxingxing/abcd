#include "../../../src/source/nullish_syntax.h"
#include "../../../tools/tree_position_json.h"
#include "abcd/source/tree_printer.h"
#include "../../support/analysis/ssa_fixture.h"
#include <algorithm>
#include <sstream>
#include <nlohmann/json.hpp>
using namespace abcd;
using namespace abcd::source;
using test::Check;
namespace {
enum class Form { kSave, kSequence, kRhsEffects, kFallback, kComputed, kOutsideCall,
                  kOldRead, kRewrite, kPartialSave, kForeign };
struct Syntax {
  FunctionTree tree{};
  ExprId root, save;
  Syntax() {
    tree.owner = {0}; tree.callable = CallableKind::kFunction; tree.strictness = Strictness::kStrict;
    tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {0};
    tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
    tree.origins = {OriginalSite{{0}, {0}}};
    tree.names = {u"value", u"seed", u"saved", u"probe", u"field", u"other", u"before", u"key"};
    for (std::uint32_t i = 0; i < 2; ++i) {
      tree.symbols.push_back({{0}, {i}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}});
      tree.parameters.push_back({{i}, {}, false});
    }
    tree.symbols.push_back({{1}, {2}, BindingKind::kCompilerTemporary, DeclarationKind::kLet,
        NameEvidence::kSynthesized, TemporaryReason::kPredicateSnapshot});
  }
  ExprId Add(TreeExpression expression) {
    const ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};
    tree.expressions.push_back({std::move(expression), {0}}); return id;
  }
  ExprId Read(unsigned symbol = 2) { return Add(SymbolExpr{{symbol}}); }
  ExprId Undefined() { return Add(TreeLiteral{UndefinedLiteral{}}); }
  ExprId Member(unsigned name) { return Add(MemberExpr{Read(0), NameId{name}}); }
  ExprId Call(unsigned name) { return Add(CallExpr{Member(name), {0, 0}, CallReference::kMember}); }
  ExprId Save(ExprId value) { return Add(AssignmentExpr{SymbolTarget{{2}}, value}); }
  ExprId Test(ExprId value, bool null) {
    return Add(BinaryExpr{analysis::BinaryOperator::kStrictNotEqual, value,
        null ? Add(TreeLiteral{NullLiteral{}}) : Undefined()});
  }
  void Build(Form form, unsigned count) {
    auto rhs = Member(3);
    if (form == Form::kRhsEffects) rhs = Add(SequenceExpr{Call(6), rhs});
    save = Save(rhs);
    ExprId result = form == Form::kFallback ? Read() : Add(MemberExpr{Read(), form == Form::kComputed ?
        MemberKey{ComputedKey{Call(7)}} : MemberKey{NameId{4}}});
    for (unsigned i = 0; i < count; ++i) {
      const auto fallback = [&] { return form == Form::kFallback ? Add(TreeLiteral{BooleanLiteral{false}}) : Undefined(); };
      const bool first = i + 1 == count;
      auto input = first && form == Form::kRewrite ? Save(Member(5)) : Read();
      const auto inner = Add(ConditionalExpr{Test(input, false), result, fallback()});
      auto outer = Test(first ? save : Read(), true);
      if (first && form == Form::kSequence) {
        // Reparent the actual save occurrence; no shared AST expression.
        std::get<BinaryExpr>(tree.expressions[outer.value].expression).left = Read();
        outer = Add(SequenceExpr{save, outer});
      }
      result = Add(ConditionalExpr{outer, inner, fallback()});
    }
    if (form == Form::kOldRead || form == Form::kPartialSave)
      result = Add(ConditionalExpr{Test(Read(), form == Form::kOldRead), result, Undefined()});
    if (form == Form::kForeign)
      result = Add(ConditionalExpr{Test(Add(AssignmentExpr{SymbolTarget{{1}}, Member(5)}), true), result, Undefined()});
    root = result;
    if (form == Form::kOutsideCall) result = Add(CallExpr{result, {0, 0}, CallReference::kValue});
    tree.statements = {{BlockStmt{{1}, 0, 2}, {0}}, {DeclarationStmt{{2}, Read(1)}, {0}}, {ReturnStmt{result}, {0}}};
    tree.children = {{1}, {2}};
  }
  void Reverse() {
    const auto map = [&](ExprId id) { return ExprId{static_cast<std::uint32_t>(tree.expressions.size() - 1 - id.value)}; };
    std::reverse(tree.expressions.begin(), tree.expressions.end());
    for (auto& node : tree.expressions) std::visit([&](auto& expression) {
      using T = std::decay_t<decltype(expression)>;
      if constexpr (std::is_same_v<T, BinaryExpr> || std::is_same_v<T, SequenceExpr>) {
        expression.left = map(expression.left); expression.right = map(expression.right);
      } else if constexpr (std::is_same_v<T, ConditionalExpr>) {
        expression.condition = map(expression.condition); expression.consequent = map(expression.consequent); expression.alternate = map(expression.alternate);
      } else if constexpr (std::is_same_v<T, MemberExpr>) {
        expression.base = map(expression.base);
        if (auto* key = std::get_if<ComputedKey>(&expression.key)) key->expression = map(key->expression);
      } else if constexpr (std::is_same_v<T, AssignmentExpr>) expression.value = map(expression.value);
      else if constexpr (std::is_same_v<T, CallExpr>) expression.callee = map(expression.callee);
    }, node.expression);
    auto& declaration = std::get<DeclarationStmt>(tree.statements[1].statement); declaration.initializer = map(*declaration.initializer);
    auto& returned = std::get<ReturnStmt>(tree.statements[2].statement); returned.value = map(*returned.value);
    root = map(root); save = map(save);
  }
};
}
int main() {
  for (unsigned form = 0; form <= unsigned(Form::kForeign); ++form)
    for (const auto count : {1u, 200u, 201u, 402u}) for (const bool reversed : {false, true}) {
      Syntax syntax; syntax.Build(Form(form), count); if (reversed) syntax.Reverse();
      const auto planned = PlanNullishSyntax(syntax.tree); Check(std::holds_alternative<NullishSyntax>(planned));
      const auto& plan = std::get<NullishSyntax>(planned);
      const bool projected = Form(form) <= Form::kOutsideCall;
      Check(bool(plan.projections[syntax.root.value]) == projected);
      if (projected) {
        Check(plan.Base(syntax.tree, syntax.root) == syntax.save);
        Check(!plan.omitted_by[syntax.save.value]);
      }
      std::ostringstream source; const auto printed = PrintFunctionTree(syntax.tree, source);
      Check(std::holds_alternative<PrintedFunction>(printed)); const auto& positions = std::get<PrintedFunction>(printed);
      for (std::size_t i = 0; i < plan.omitted_by.size(); ++i) if (plan.omitted_by[i]) {
        const auto base = plan.Base(syntax.tree, *plan.omitted_by[i]);
        Check(positions.expressions[i] == GeneratedRange{positions.expressions[base.value].end, positions.expressions[base.value].end});
      } else Check(positions.expressions[i].begin < positions.expressions[i].end);
      std::ostringstream mapping; tools::WriteTreePositions(mapping, syntax.tree, positions); Check(bool(mapping));
      const auto json = nlohmann::json::parse(mapping.str());
      for (std::size_t i = 0; i < plan.omitted_by.size(); ++i) if (plan.omitted_by[i])
        Check(json["expressions"][i]["projection"] == plan.omitted_by[i]->value);
      std::cout << nlohmann::json{{"form", form}, {"count", count}, {"reversed", reversed}, {"source", source.str()}, {"work", plan.work}} << '\n';
    }
}
