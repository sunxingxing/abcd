#include "../../../src/source/nullish_syntax.h"
#include "abcd/source/tree_printer.h"
#include "../../../tools/tree_position_json.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>
#include <source_location>
#include <sstream>

using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool value, std::source_location site = std::source_location::current()) {
  if (!value) {std::cerr << "check failed at " << site.line() << '\n'; std::abort();}
}
struct Syntax {
  FunctionTree tree{};
  ExprId root, base;
  std::vector<ExprId> members;
  Syntax(unsigned depth, bool computed, bool call, bool formed) {
    tree.owner = {0}; tree.callable = CallableKind::kFunction; tree.strictness = Strictness::kStrict;
    tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {0};
    tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
    tree.origins = {OriginalSite{{0}, {0}}}; tree.names = {u"value", u"key", u"source", u"field"};
    for (std::uint32_t i = 0; i < 3; ++i) {
      tree.symbols.push_back({{0}, {i}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}});
      tree.parameters.push_back({{i}, {}, false});
    }
    base = Add(SymbolExpr{{0}});
    auto result = base;
    for (unsigned i = 0; i < depth; ++i) {
      const MemberKey key = computed ? MemberKey{ComputedKey{Add(SymbolExpr{{1}})}} : MemberKey{NameId{3}};
      result = Add(MemberExpr{result, key}); members.push_back(result);
    }
    if (call) {
      const auto argument = Add(CallExpr{Add(SymbolExpr{{1}}), {0, 0}, CallReference::kValue});
      tree.arguments.push_back(argument);
      result = Add(CallExpr{result, {0, 1}, CallReference::kMember});
    }
    const auto first = formed ? Add(AssignmentExpr{SymbolTarget{{0}}, Add(CallExpr{Add(SymbolExpr{{2}}), {0, 0}, CallReference::kValue})}) :
      Add(SymbolExpr{{0}});
    const auto nil = Add(BinaryExpr{analysis::BinaryOperator::kStrictNotEqual, first, Add(TreeLiteral{NullLiteral{}})});
    const auto absent = Add(BinaryExpr{analysis::BinaryOperator::kStrictNotEqual, Add(SymbolExpr{{0}}), Add(TreeLiteral{UndefinedLiteral{}})});
    const auto inner = Add(ConditionalExpr{absent, result, Add(TreeLiteral{UndefinedLiteral{}})});
    root = Add(ConditionalExpr{nil, inner, Add(TreeLiteral{UndefinedLiteral{}})});
    tree.statements = {{BlockStmt{{1}, 0, 1}, {0}}, {ReturnStmt{root}, {0}}}; tree.children = {{1}};
  }
  ExprId Add(TreeExpression node) {
    const ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};
    tree.expressions.push_back({std::move(node), {0}}); return id;
  }
  void Reverse() {
    const auto map = [&](ExprId id) {return ExprId{static_cast<std::uint32_t>(tree.expressions.size() - 1 - id.value)};};
    std::reverse(tree.expressions.begin(), tree.expressions.end());
    for (auto& expression : tree.expressions) std::visit([&](auto& node) {
      using T = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<T, MemberExpr>) {
        node.base = map(node.base); if (auto* key = std::get_if<ComputedKey>(&node.key)) key->expression = map(key->expression);
      } else if constexpr (std::is_same_v<T, BinaryExpr>) {node.left = map(node.left); node.right = map(node.right);}
      else if constexpr (std::is_same_v<T, ConditionalExpr>) {node.condition = map(node.condition); node.consequent = map(node.consequent); node.alternate = map(node.alternate);}
      else if constexpr (std::is_same_v<T, AssignmentExpr>) node.value = map(node.value);
      else if constexpr (std::is_same_v<T, CallExpr>) node.callee = map(node.callee);
    }, expression.expression);
    for (auto& argument : tree.arguments) argument = map(argument);
    for (auto& member : members) member = map(member);
    base = map(base); root = map(root); std::get<ReturnStmt>(tree.statements[1].statement).value = root;
  }
};
}
int main(int argc, char**) {
  const bool emit = argc > 1;
  std::array<std::uint64_t, 16> baseline{};
  for (const unsigned depth : {1u, 2u, 3u, 200u, 201u, 402u})
    for (const bool computed : {false, true}) for (const bool call : {false, true})
      for (const bool formed : {false, true}) for (const bool reversed : {false, true}) {
        Syntax syntax(depth, computed, call, formed); if (reversed) syntax.Reverse();
        const auto planned = PlanNullishSyntax(syntax.tree); Check(std::holds_alternative<NullishSyntax>(planned));
        const auto& plan = std::get<NullishSyntax>(planned);
        if (!plan.projections[syntax.root.value]) std::cerr << "depth=" << depth << " computed=" << computed << " call=" << call
          << " formed=" << formed << " reversed=" << reversed << " missing projection\n";
        Check(plan.projections[syntax.root.value].has_value());
        Check(std::count_if(plan.projections.begin(), plan.projections.end(), [](const auto& value) {return bool(value);}) == 1);
        std::ostringstream text; const auto printed = PrintFunctionTree(syntax.tree, text);
        if (const auto* error = std::get_if<TreePrintError>(&printed))
          std::cerr << "depth=" << depth << " computed=" << computed << " call=" << call << " formed=" << formed
            << " reversed=" << reversed << " print=" << unsigned(*error) << '\n';
        Check(std::holds_alternative<PrintedFunction>(printed)); const auto& positions = std::get<PrintedFunction>(printed);
        for (const auto member : syntax.members) {
          Check(!plan.omitted_by[member.value]); Check(positions.expressions[member.value].begin < positions.expressions[member.value].end);
        }
        const auto base = plan.Base(syntax.tree, syntax.root);
        for (std::size_t i = 0; i < plan.omitted_by.size(); ++i) if (plan.omitted_by[i])
          Check(positions.expressions[i] == GeneratedRange{positions.expressions[base.value].end, positions.expressions[base.value].end});
        std::ostringstream mapping; tools::WriteTreePositions(mapping, syntax.tree, positions); Check(bool(mapping));
        const auto shape = (computed ? 8u : 0u) + (call ? 4u : 0u) + (formed ? 2u : 0u) + unsigned(reversed);
        if (depth == 201) baseline[shape] = plan.work;
        if (depth == 402) Check(plan.work < 3 * baseline[shape]);
        if (emit) std::cout << nlohmann::json{{"depth", depth}, {"computed", computed}, {"call", call},
          {"formed", formed}, {"reversed", reversed}, {"source", text.str()}} << '\n';
      }
}
