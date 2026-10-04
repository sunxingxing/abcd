#include "../../../src/source/nullish_syntax.h"
#include "abcd/source/tree_printer.h"
#include "../../../tools/tree_position_json.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <source_location>
#include <nlohmann/json.hpp>
using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool ok, std::source_location at = std::source_location::current()) {
  if (!ok) { std::cerr << "check failed at " << at.line() << '\n'; std::abort(); }
}
enum class Form { kNested, kReversed, kAnd, kOr, kInverted, kComputed, kCall,
    kPredicateSelection, kOuterMember, kMissingNull, kDistinctBase, kDifferentFallback,
    kTruthiness, kFallbackEffect, kEffectfulBase, kEffectfulGuard, kDeadCall, kDeadAggregate, kQuotedKey };
struct Syntax {
  FunctionTree tree{};
  ExprId selection, member;
  Syntax() {
    tree.owner = {0}; tree.callable = CallableKind::kFunction; tree.strictness = Strictness::kStrict;
    tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {0};
    tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
    tree.origins = {OriginalSite{{0}, {0}}}; tree.names = {u"value", u"key", u"field"};
    for (std::uint32_t i = 0; i < 2; ++i) {
      tree.symbols.push_back({{0}, {i}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}});
      tree.parameters.push_back({{i}, {}, false});
    }
  }
  ExprId Add(TreeExpression expression) {
    const ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};
    tree.expressions.push_back({std::move(expression), {0}}); return id;
  }
  ExprId Value(std::uint32_t symbol = 0) { return Add(SymbolExpr{{symbol}}); }
  ExprId Undefined() { return Add(TreeLiteral{UndefinedLiteral{}}); }
  ExprId Test(bool null, bool equal = false) {
    const auto input = Value();
    const auto constant = null ? Add(TreeLiteral{NullLiteral{}}) : Undefined();
    return Add(BinaryExpr{equal ? analysis::BinaryOperator::kStrictEqual : analysis::BinaryOperator::kStrictNotEqual,
        input, constant});
  }
  void Build(Form form, unsigned count = 1) {
    if (form == Form::kQuotedKey) tree.names[2] = u"not-an-identifier";
    auto base = Value(form == Form::kDistinctBase ? 1 : 0);
    if (form == Form::kEffectfulBase) base = Add(MemberExpr{base, NameId{2}});
    member = Add(MemberExpr{base, form == Form::kComputed ? MemberKey{ComputedKey{Value(1)}} : MemberKey{NameId{2}}});
    auto result = member;
    for (unsigned i = 0; i < count; ++i) {
      const auto fallback = [&] {
        if (form == Form::kFallbackEffect) return Add(CallExpr{Value(1), {0, 0}, CallReference::kValue});
        return form == Form::kDifferentFallback ? Add(TreeLiteral{NullLiteral{}}) : Undefined();
      };
      if (form == Form::kPredicateSelection) {
        const auto condition = Add(ConditionalExpr{Test(true, true), Add(TreeLiteral{BooleanLiteral{false}}), Test(false)});
        result = Add(ConditionalExpr{condition, result, fallback()});
      } else if (form == Form::kAnd || form == Form::kOr) {
        const auto predicate = Add(LogicalExpr{form == Form::kAnd ? TreeLogical::kAnd : TreeLogical::kOr,
            Test(true, form == Form::kOr), Test(false, form == Form::kOr)});
        result = form == Form::kAnd ? Add(ConditionalExpr{predicate, result, fallback()}) :
            Add(ConditionalExpr{predicate, fallback(), result});
      } else if (form == Form::kMissingNull || form == Form::kTruthiness) {
        result = Add(ConditionalExpr{form == Form::kTruthiness ? Value() : Test(false), result, fallback()});
      } else {
        const bool reverse = form == Form::kReversed;
        const auto inner = Add(ConditionalExpr{Test(reverse), result, fallback()});
        auto outer = Test(!reverse);
        if (form == Form::kEffectfulGuard)
          outer = Add(SequenceExpr{Add(CallExpr{Value(1), {0, 0}, CallReference::kValue}), outer});
        result = form == Form::kInverted ? Add(ConditionalExpr{Add(UnaryExpr{TreeUnary::kNot, outer}), fallback(), inner}) :
            Add(ConditionalExpr{outer, inner, fallback()});
      }
    }
    if (form == Form::kDeadCall || form == Form::kDeadAggregate) {
      const auto argument = Add(MemberExpr{Value(), NameId{2}});
      tree.arguments.push_back(argument);
      auto dead = Add(CallExpr{Value(1), {0, 1}, CallReference::kValue});
      if (form == Form::kDeadAggregate) {
        tree.properties.push_back({NameId{2}, dead, {0}});
        dead = Add(ObjectExpr{{0, 1}});
      }
      result = Add(ConditionalExpr{Add(TreeLiteral{BooleanLiteral{true}}), result,
          dead});
    }
    selection = result;
    if (form == Form::kCall) result = Add(CallExpr{result, {0, 0}, CallReference::kValue});
    if (form == Form::kOuterMember) result = Add(MemberExpr{result, NameId{2}});
    tree.statements = {{BlockStmt{{1}, 0, 1}, {0}}, {ReturnStmt{result}, {0}}}; tree.children = {{1}};
  }
  void ReverseArena() {
    const auto remap = [&](ExprId id) { return ExprId{static_cast<std::uint32_t>(tree.expressions.size() - 1 - id.value)}; };
    std::reverse(tree.expressions.begin(), tree.expressions.end());
    for (auto& expression : tree.expressions)
      std::visit([&](auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, BinaryExpr> || std::is_same_v<T, LogicalExpr> || std::is_same_v<T, SequenceExpr>) {
          node.left = remap(node.left); node.right = remap(node.right);
        } else if constexpr (std::is_same_v<T, UnaryExpr>) node.operand = remap(node.operand);
        else if constexpr (std::is_same_v<T, ConditionalExpr>) {
          node.condition = remap(node.condition); node.consequent = remap(node.consequent); node.alternate = remap(node.alternate);
        } else if constexpr (std::is_same_v<T, MemberExpr>) {
          node.base = remap(node.base);
          if (auto* key = std::get_if<ComputedKey>(&node.key)) key->expression = remap(key->expression);
        } else if constexpr (std::is_same_v<T, CallExpr>) node.callee = remap(node.callee);
      }, expression.expression);
    auto& returned = std::get<ReturnStmt>(tree.statements[1].statement);
    returned.value = remap(*returned.value); selection = remap(selection); member = remap(member);
    for (auto& argument : tree.arguments) argument = remap(argument);
    for (auto& property : tree.properties) property.value = remap(property.value);
  }
};
}
int main(int argc, char** argv) {
  const bool plans_only = argc == 2 && std::string_view(argv[1]) == "--plan";
  for (const auto count : {1u, 200u, 201u, 402u})
    for (const auto form : {Form::kNested, Form::kReversed, Form::kAnd, Form::kOr, Form::kInverted,
        Form::kComputed, Form::kCall, Form::kPredicateSelection, Form::kOuterMember, Form::kMissingNull,
        Form::kDistinctBase, Form::kDifferentFallback, Form::kTruthiness, Form::kFallbackEffect,
        Form::kEffectfulBase, Form::kEffectfulGuard, Form::kDeadCall, Form::kDeadAggregate, Form::kQuotedKey})
    for (const bool reversed : {false, true}) {
      Syntax syntax; syntax.Build(form, count);
      if (reversed) syntax.ReverseArena();
      const bool accepted = form <= Form::kOuterMember || form >= Form::kDeadCall;
      const auto result = PlanNullishSyntax(syntax.tree);
      Check(std::holds_alternative<NullishSyntax>(result));
      const auto& plan = std::get<NullishSyntax>(result);
      Check((!plan.projections.empty() && bool(plan.projections[syntax.selection.value])) == accepted);
      if (accepted) {
        Check(std::get<OptionalMemberSyntax>(*plan.projections[syntax.selection.value]).member == syntax.member);
        Check(std::count_if(plan.projections.begin(), plan.projections.end(), [](const auto& projection) { return bool(projection); }) == 1);
        Check(!plan.omitted_by[syntax.member.value]);
        Check(!plan.omitted_by[std::get<MemberExpr>(syntax.tree.expressions[syntax.member.value].expression).base.value]);
      }
      if (plans_only) continue;
      std::ostringstream source;
      const auto printed = PrintFunctionTree(syntax.tree, source);
      Check(std::holds_alternative<PrintedFunction>(printed));
      const auto& positions = std::get<PrintedFunction>(printed);
      std::cout << nlohmann::json{{"count", count}, {"form", unsigned(form)}, {"optional", accepted}, {"reversed", reversed},
          {"source", source.str()}, {"work", plan.work}} << '\n';
      if (accepted) {
        for (std::size_t i = 0; i < plan.omitted_by.size(); ++i)
          if (plan.omitted_by[i]) {
            const auto base = std::get<MemberExpr>(syntax.tree.expressions[syntax.member.value].expression).base;
            Check(positions.expressions[i] == GeneratedRange{positions.expressions[base.value].end, positions.expressions[base.value].end});
          } else Check(positions.expressions[i].begin < positions.expressions[i].end);
        for (std::size_t i = 0; i < plan.omitted_properties_by.size(); ++i)
          if (plan.omitted_properties_by[i]) Check(positions.properties[i].begin == positions.properties[i].end);
      }
      if (count == 1) {
        std::ostringstream mapping;
        tools::WriteTreePositions(mapping, syntax.tree, positions);
        Check(bool(mapping));
        const auto json = nlohmann::json::parse(mapping.str());
        for (std::size_t i = 0; i < plan.omitted_by.size(); ++i)
          if (plan.omitted_by[i]) {
            Check(json["expressions"][i]["emission"] == "optional_access");
            Check(json["expressions"][i]["projection"] == plan.omitted_by[i]->value);
          }
        for (std::size_t i = 0; i < plan.omitted_properties_by.size(); ++i)
          if (plan.omitted_properties_by[i]) {
            Check(json["properties"][i]["emission"] == "optional_access");
            Check(json["properties"][i]["projection"] == plan.omitted_properties_by[i]->value);
          }
      }
    }
}
