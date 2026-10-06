#include "../../../src/source/nullish_syntax.h"
#include "abcd/source/tree_printer.h"
#include "../../support/analysis/ssa_fixture.h"
#include <algorithm>
#include <array>
#include <nlohmann/json.hpp>
#include <sstream>

using namespace abcd;
using namespace abcd::source;
using test::Check;
namespace {
struct Syntax {
  FunctionTree tree{};
  ExprId root;
  std::vector<ExprId> links;
  Syntax(unsigned depth, bool computed, bool first_call, bool tail_call, bool formed, bool reversed,
      bool detached = false, bool returned_call = false) {
    tree.owner = {0}; tree.callable = CallableKind::kFunction; tree.strictness = Strictness::kStrict;
    tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {0};
    tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
    tree.origins = {OriginalSite{{0}, {0}}}; tree.names = {u"value", u"key", u"source", u"field"};
    for (std::uint32_t i = 0; i < 3; ++i) {
      tree.symbols.push_back({{0}, {i}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}});
      tree.parameters.push_back({{i}, {}, false});
    }
    auto value = E(SymbolExpr{{0}});
    if (first_call) { value = Call(value, CallReference::kValue); links.push_back(value); }
    for (unsigned i = 0; i < depth; ++i) {
      value = E(MemberExpr{value, computed ? MemberKey{ComputedKey{E(SymbolExpr{{1}})}} : MemberKey{NameId{3}}});
      links.push_back(value);
      if (i + 1 < depth || tail_call) {
        value = Call(value, detached ? CallReference::kValue : CallReference::kMember); links.push_back(value);
        if (returned_call) {value = Call(value, CallReference::kValue); links.push_back(value);}
      }
    }
    const auto first = formed ? E(AssignmentExpr{SymbolTarget{{0}}, E(CallExpr{E(SymbolExpr{{2}}), {0, 0}, CallReference::kValue})}) : E(SymbolExpr{{0}});
    const auto nil = E(BinaryExpr{analysis::BinaryOperator::kStrictNotEqual, first, E(TreeLiteral{NullLiteral{}})});
    const auto absent = E(BinaryExpr{analysis::BinaryOperator::kStrictNotEqual, E(SymbolExpr{{0}}), E(TreeLiteral{UndefinedLiteral{}})});
    root = E(ConditionalExpr{nil, E(ConditionalExpr{absent, value, E(TreeLiteral{UndefinedLiteral{}})}), E(TreeLiteral{UndefinedLiteral{}})});
    tree.statements = {{BlockStmt{{1}, 0, 1}, {0}}, {ReturnStmt{root}, {0}}}; tree.children = {{1}};
    if (reversed) Reverse();
  }
  ExprId E(TreeExpression expression) {
    const ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};
    tree.expressions.push_back({std::move(expression), {0}}); return id;
  }
  ExprId Call(ExprId callee, CallReference reference) {
    const auto argument = E(CallExpr{E(SymbolExpr{{1}}), {0, 0}, CallReference::kValue});
    const auto first = static_cast<std::uint32_t>(tree.arguments.size()); tree.arguments.push_back(argument);
    return E(CallExpr{callee, {first, 1}, reference});
  }
  void Reverse() {
    const auto map = [&](ExprId id) { return ExprId{static_cast<std::uint32_t>(tree.expressions.size() - 1 - id.value)}; };
    std::reverse(tree.expressions.begin(), tree.expressions.end());
    for (auto& expression : tree.expressions) std::visit([&](auto& node) {
      using T = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<T, MemberExpr>) {
        node.base = map(node.base); if (auto* key = std::get_if<ComputedKey>(&node.key)) key->expression = map(key->expression);
      } else if constexpr (std::is_same_v<T, CallExpr>) node.callee = map(node.callee);
      else if constexpr (std::is_same_v<T, AssignmentExpr>) node.value = map(node.value);
      else if constexpr (std::is_same_v<T, BinaryExpr>) { node.left = map(node.left); node.right = map(node.right); }
      else if constexpr (std::is_same_v<T, ConditionalExpr>) { node.condition = map(node.condition); node.consequent = map(node.consequent); node.alternate = map(node.alternate); }
    }, expression.expression);
    for (auto& argument : tree.arguments) argument = map(argument);
    for (auto& link : links) link = map(link);
    root = map(root); std::get<ReturnStmt>(tree.statements[1].statement).value = root;
  }
};
}
int main(int argc, char**) {
  std::array<std::uint64_t, 32> baseline{};
  for (const unsigned depth : {1u, 2u, 3u, 200u, 201u, 402u})
    for (const bool computed : {false, true}) for (const bool first_call : {false, true})
      for (const bool tail_call : {false, true}) for (const bool formed : {false, true}) for (const bool reversed : {false, true}) {
        Syntax syntax(depth, computed, first_call, tail_call, formed, reversed);
        const auto planned = PlanNullishSyntax(syntax.tree); Check(std::holds_alternative<NullishSyntax>(planned));
        const auto& plan = std::get<NullishSyntax>(planned);
        Check(plan.projections[syntax.root.value].has_value());
        std::ostringstream source; const auto printed = PrintFunctionTree(syntax.tree, source);
        Check(std::holds_alternative<PrintedFunction>(printed));
        const auto& positions = std::get<PrintedFunction>(printed);
        for (const auto link : syntax.links) {
          Check(!plan.omitted_by[link.value]); Check(positions.expressions[link.value].begin < positions.expressions[link.value].end);
        }
        const auto boundary = positions.expressions[plan.Base(syntax.tree, syntax.root).value].end;
        for (std::size_t i = 0; i < plan.omitted_by.size(); ++i) if (plan.omitted_by[i])
          Check(positions.expressions[i] == GeneratedRange{boundary, boundary});
        const auto shape = (computed ? 16u : 0u) + (first_call ? 8u : 0u) + (tail_call ? 4u : 0u) + (formed ? 2u : 0u) + unsigned(reversed);
        if (depth == 201) baseline[shape] = plan.work;
        if (depth == 402) Check(plan.work < 3 * baseline[shape]);
        if (argc > 1) std::cout << nlohmann::json{{"depth", depth}, {"computed", computed}, {"firstCall", first_call},
          {"tailCall", tail_call}, {"formed", formed}, {"reversed", reversed}, {"source", source.str()}} << '\n';
      }
  // A value call on a member deliberately discards its receiver. Native member
  // chaining would reattach it; preserve the original conditional expression.
  for (const bool computed : {false, true}) {
    Syntax detached(2, computed, false, false, false, false, true);
    const auto plan = std::get<NullishSyntax>(PlanNullishSyntax(detached.tree));
    Check(!plan.projections[detached.root.value]);
    std::ostringstream source; Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(detached.tree, source)));
    Check(source.str().find(computed ? "(0, value[key])" : "(0, value.field)") != std::string::npos);
    if (argc > 1) std::cout << nlohmann::json{{"depth", 2}, {"computed", computed}, {"detached", true}, {"source", source.str()}} << '\n';
  }
  // A call result is a value, so its next invocation must keep an undefined
  // receiver. It can continue the same native chain without member detachment.
  for (const bool computed : {false, true}) for (const bool formed : {false, true}) for (const bool reversed : {false, true}) {
    Syntax syntax(3, computed, true, true, formed, reversed, false, true);
    const auto plan = std::get<NullishSyntax>(PlanNullishSyntax(syntax.tree));
    Check(plan.projections[syntax.root.value].has_value());
    std::ostringstream source; const auto printed = PrintFunctionTree(syntax.tree, source);
    Check(std::holds_alternative<PrintedFunction>(printed));
    for (const auto link : syntax.links) {
      Check(!plan.omitted_by[link.value]);
      const auto range = std::get<PrintedFunction>(printed).expressions[link.value]; Check(range.begin < range.end);
    }
    if (argc > 1) std::cout << nlohmann::json{{"depth", 3}, {"computed", computed}, {"firstCall", true},
      {"tailCall", true}, {"formed", formed}, {"reversed", reversed}, {"repeated", true}, {"source", source.str()}} << '\n';
  }
  // Passing intrinsic eval as the optional callable must keep indirect eval.
  // A direct-eval mutation can observe the original lexical argument binding.
  Syntax eval(0, false, true, true, false, false);
  eval.tree.names[1] = u"__abcdOptionalEvalArgument";
  const auto plan = std::get<NullishSyntax>(PlanNullishSyntax(eval.tree)); Check(plan.projections[eval.root.value].has_value());
  std::ostringstream source; Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(eval.tree, source)));
  if (argc > 1) std::cout << nlohmann::json{{"eval", true}, {"source", source.str()}} << '\n';
}
