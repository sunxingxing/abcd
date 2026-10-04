#include "abcd/source/tree_printer.h"
#include "../../../src/source/nullish_syntax.h"
#include "../../../tools/tree_position_json.h"
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <source_location>
#include <type_traits>
#include <nlohmann/json.hpp>
using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool ok, std::source_location at = std::source_location::current()) {
  if (!ok) { std::cerr << "check failed at " << at.line() << '\n'; std::abort(); }
}
enum class Form { kOr, kNested, kReversed, kAnd, kInverted, kCallFallback, kMemberFallback,
    kOutsideCall, kMixedLogic, kMissingUndefined, kTruthiness, kDifferentValue, kEffectfulGuard, kDifferentFallback,
    kEqualText, kDifferentText, kBindingFallback, kForeignSelection, kForeignUniform, kSignedZero, kSequenceFallback,
    kForeignGuard };
struct Syntax {
  FunctionTree tree{};
  Syntax() {
    tree.owner = {0}; tree.callable = CallableKind::kFunction; tree.strictness = Strictness::kStrict;
    tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {0};
    tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
    tree.origins = {OriginalSite{{0}, {0}}}; tree.names = {u"value", u"fallback", u"other", u"field"};
    tree.texts = {u"fallback-text", u"fallback-text", u"different-text"};
    for (std::uint32_t i = 0; i < 3; ++i) {
      tree.symbols.push_back({{0}, {i}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}});
      tree.parameters.push_back({{i}, {}, false});
    }
  }
  ExprId Add(TreeExpression expression) {
    const ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};
    tree.expressions.push_back({std::move(expression), {0}}); return id;
  }
  ExprId Value(std::uint32_t symbol = 0) { return Add(SymbolExpr{{symbol}}); }
  ExprId False() { return Add(TreeLiteral{BooleanLiteral{false}}); }
  ExprId Call(std::uint32_t symbol) { return Add(CallExpr{Value(symbol), {0, 0}, CallReference::kValue}); }
  ExprId Test(bool null, bool equal = false) {
    return Add(BinaryExpr{equal ? analysis::BinaryOperator::kStrictEqual : analysis::BinaryOperator::kStrictNotEqual,
        Value(), null ? Add(TreeLiteral{NullLiteral{}}) : Add(TreeLiteral{UndefinedLiteral{}})});
  }
  ExprId ForeignTest() {
    return Add(BinaryExpr{analysis::BinaryOperator::kStrictEqual, Value(2), Add(TreeLiteral{NullLiteral{}})});
  }
  void Build(Form form, unsigned count, bool reversed) {
    auto result = Value(form == Form::kDifferentValue ? 2 : 0);
    for (unsigned i = 0; i < count; ++i) {
      const auto fallback = [&] {
        if (form == Form::kCallFallback) return Call(1);
        if (form == Form::kMemberFallback) return Add(MemberExpr{Value(1), NameId{3}});
        if (form == Form::kBindingFallback) return Value(1);
        if (form == Form::kForeignSelection) return Add(ConditionalExpr{ForeignTest(), Call(1), False()});
        if (form == Form::kForeignUniform) return Add(ConditionalExpr{ForeignTest(), False(), False()});
        if (form == Form::kSequenceFallback) return Add(SequenceExpr{Call(2), Call(1)});
        if (form == Form::kEqualText || form == Form::kDifferentText) return Add(TreeLiteral{TextId{0}});
        if (form == Form::kSignedZero) return Add(TreeLiteral{NumberLiteral{std::bit_cast<std::uint64_t>(0.0)}});
        return False();
      };
      if (form == Form::kForeignGuard) {
        const auto dependent = Add(ConditionalExpr{Test(true, true), ForeignTest(), False()});
        const auto guard = Add(LogicalExpr{TreeLogical::kOr, dependent,
            Add(LogicalExpr{TreeLogical::kOr, Test(true, true), Test(false, true)})});
        result = Add(ConditionalExpr{guard, False(), result});
      } else if (form == Form::kOr || form == Form::kCallFallback || form == Form::kMemberFallback ||
          form == Form::kOutsideCall || form == Form::kMixedLogic || form == Form::kForeignSelection ||
          form == Form::kSequenceFallback) {
        result = Add(ConditionalExpr{Add(LogicalExpr{TreeLogical::kOr, Test(true, true), Test(false, true)}), fallback(), result});
      } else if (form == Form::kAnd) {
        result = Add(ConditionalExpr{Add(LogicalExpr{TreeLogical::kAnd, Test(true), Test(false)}), result, fallback()});
      } else if (form == Form::kMissingUndefined || form == Form::kTruthiness) {
        result = Add(ConditionalExpr{form == Form::kTruthiness ? Value() : Test(true), result, fallback()});
      } else {
        const bool reverse = form == Form::kReversed;
        const auto inner_fallback = [&] {
          if (form == Form::kDifferentFallback) return Add(TreeLiteral{IntegerLiteral{0}});
          if (form == Form::kEqualText || form == Form::kDifferentText)
            return Add(TreeLiteral{TextId{form == Form::kEqualText ? 1u : 2u}});
          if (form == Form::kSignedZero) return Add(TreeLiteral{NumberLiteral{std::bit_cast<std::uint64_t>(-0.0)}});
          return fallback();
        };
        const auto inner = Add(ConditionalExpr{Test(reverse), result, inner_fallback()});
        auto outer = Test(!reverse);
        if (form == Form::kEffectfulGuard) outer = Add(SequenceExpr{Call(2), outer});
        result = form == Form::kInverted ? Add(ConditionalExpr{Add(UnaryExpr{TreeUnary::kNot, outer}), fallback(), inner}) :
            Add(ConditionalExpr{outer, inner, fallback()});
      }
    }
    if (form == Form::kOutsideCall) result = Add(CallExpr{result, {0, 0}, CallReference::kValue});
    if (form == Form::kMixedLogic) result = Add(LogicalExpr{TreeLogical::kOr, result, Call(2)});
    tree.statements = {{BlockStmt{{1}, 0, 1}, {0}}, {ReturnStmt{result}, {0}}}; tree.children = {{1}};
    if (form == Form::kForeignUniform || form == Form::kForeignGuard) {
      tree.parameters.pop_back();
      tree.symbols[2].scope = {1}; tree.symbols[2].kind = BindingKind::kLocal;
      tree.symbols[2].declaration = DeclarationKind::kLet;
      tree.statements.push_back({DeclarationStmt{{2}, {}}, {0}});
      tree.children.push_back({2}); std::get<BlockStmt>(tree.statements[0].statement).child_count = 2;
    }
    if (reversed) {
      const auto remap = [&](ExprId id) { return ExprId{static_cast<std::uint32_t>(tree.expressions.size() - 1 - id.value)}; };
      std::reverse(tree.expressions.begin(), tree.expressions.end());
      for (auto& expression : tree.expressions) std::visit([&](auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, BinaryExpr> || std::is_same_v<T, LogicalExpr> || std::is_same_v<T, SequenceExpr>) {
          node.left = remap(node.left); node.right = remap(node.right);
        } else if constexpr (std::is_same_v<T, UnaryExpr>) node.operand = remap(node.operand);
        else if constexpr (std::is_same_v<T, ConditionalExpr>) {
          node.condition = remap(node.condition); node.consequent = remap(node.consequent); node.alternate = remap(node.alternate);
        } else if constexpr (std::is_same_v<T, MemberExpr>) node.base = remap(node.base);
        else if constexpr (std::is_same_v<T, CallExpr>) node.callee = remap(node.callee);
      }, expression.expression);
      auto& returned = std::get<ReturnStmt>(tree.statements[1].statement); returned.value = remap(*returned.value);
    }
  }
};
}
int main() {
  for (const auto count : {1u, 200u, 201u, 402u}) for (unsigned form = 0; form <= unsigned(Form::kForeignGuard); ++form)
    for (const bool reversed : {false, true}) {
      Syntax syntax; syntax.Build(Form(form), count, reversed);
      std::ostringstream source;
      const auto printed = PrintFunctionTree(syntax.tree, source);
      Check(std::holds_alternative<PrintedFunction>(printed));
      const bool accepted = Form(form) <= Form::kMixedLogic || Form(form) == Form::kEqualText ||
          Form(form) == Form::kBindingFallback || Form(form) == Form::kForeignSelection || Form(form) == Form::kSequenceFallback;
      Check((source.str().find("??") != std::string::npos) == accepted);
      const auto planned = PlanNullishSyntax(syntax.tree);
      Check(std::holds_alternative<NullishSyntax>(planned));
      const auto& plan = std::get<NullishSyntax>(planned);
      const auto& positions = std::get<PrintedFunction>(printed);
      for (std::size_t i = 0; i < plan.omitted_by.size(); ++i) {
        if (plan.omitted_by[i]) {
          const auto root = *plan.omitted_by[i], base = plan.Base(syntax.tree, root);
          Check(positions.expressions[i] == GeneratedRange{positions.expressions[base.value].end, positions.expressions[base.value].end});
        } else Check(positions.expressions[i].begin < positions.expressions[i].end);
      }
      if (count == 1) {
        std::ostringstream mapping;
        tools::WriteTreePositions(mapping, syntax.tree, positions);
        Check(bool(mapping));
        const auto json = nlohmann::json::parse(mapping.str());
        for (std::size_t i = 0; i < plan.omitted_by.size(); ++i) if (plan.omitted_by[i])
          Check(json["expressions"][i]["emission"] == "nullish_fallback");
      }
      std::cout << nlohmann::json{{"count", count}, {"form", form}, {"reversed", reversed},
          {"nullish", accepted}, {"source", source.str()}} << '\n';
    }
}
