#include "../../../src/source/family_print.h"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <source_location>
#include <string_view>
using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool ok, std::source_location at = std::source_location::current()) {
  if (!ok) { std::cerr << "check failed at " << at.line() << '\n'; std::abort(); }
}
FunctionTree Base(unsigned owner) {
  FunctionTree tree{};
  tree.owner = {owner}; tree.callable = CallableKind::kFunction; tree.strictness = Strictness::kStrict;
  tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {0};
  tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
  tree.origins.emplace_back(OriginalSite{{0}, {0}});
  tree.statements = {{BlockStmt{{1}, 0, 1}, {0}}, {ReturnStmt{ExprId{0}}, {0}}};
  tree.children = {{1}};
  return tree;
}
ConstructedFamily Fixture() {
  auto root = Base(10), reader = Base(20), receiver = Base(30);
  root.names = {u"state", u"reader", u"receiver"};
  for (unsigned i = 0; i != 3; ++i)
    root.symbols.push_back({{1}, {i}, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kSynthesized, {}});
  root.expressions = {{TreeLiteral{IntegerLiteral{1}}, {0}}, {FunctionExpr{{20}, {0, 1}}, {0}},
      {FunctionExpr{{30}, {1, 0}}, {0}}, {TreeLiteral{IntegerLiteral{42}}, {0}},
      {AssignmentExpr{SymbolTarget{{0}}, {3}}, {0}}};
  root.closure_captures = {{0}};
  root.statements = {{BlockStmt{{1}, 0, 4}, {0}}, {DeclarationStmt{{0}, ExprId{0}}, {0}},
      {DeclarationStmt{{1}, ExprId{1}}, {0}}, {DeclarationStmt{{2}, ExprId{2}}, {0}},
      {ReturnStmt{ExprId{4}}, {0}}};
  root.children = {{1}, {2}, {3}, {4}};
  reader.names = {u"state"}; reader.capture_scope = ScopeId{2};
  reader.scopes.push_back({ScopeKind::kCaptures, {}}); reader.scopes[0].parent = ScopeId{2};
  reader.symbols = {{{2}, {0}, BindingKind::kCapture, DeclarationKind::kLet, NameEvidence::kSynthesized, {}}};
  reader.captures = {{{0}, {{10}, {0}}}};
  reader.expressions = {{SymbolExpr{{0}}, {0}}};
  receiver.expressions = {{ThisExpr{{30}}, {0}}};
  // Deliberately store root last: module scope follows the root ID, not index 0.
  ConstructedFamily family; family.root = {2};
  family.members = {{std::move(reader), {}, {}}, {std::move(receiver), {}, {}}, {std::move(root), {}, {}}};
  return family;
}
}
int main(int argc, char** argv) {
  auto family = Fixture();
  const std::string_view mode = argc > 1 ? argv[1] : "";
  auto& receiver = family.members[1].tree;
  if (mode == "--generator" || mode == "--async") {
    receiver.callable = mode == "--generator" ? CallableKind::kGenerator : CallableKind::kAsyncFunction;
    receiver.expressions.push_back({mode == "--generator" ? TreeExpression{YieldExpr{{0}}} :
        TreeExpression{AwaitExpr{{0}}}, {0}});
    receiver.statements[1].statement = ReturnStmt{ExprId{1}};
  }
  std::ostringstream output;
  const auto result = PrintModuleFamilyDraft(family, output);
  if (const auto* e = std::get_if<TreePrintError>(&result)) std::cerr << "print error " << unsigned(*e) << '\n';
  Check(std::holds_alternative<PrintedFamilyDraft>(result));
  const auto& printed = std::get<PrintedFamilyDraft>(result);
  Check(printed.bytes == output.str().size());
  Check(printed.members[2].fragment == FunctionFragment::kModuleBody);
  Check(printed.members[0].fragment == FunctionFragment::kConditionalExpression);
  Check(printed.members[1].fragment == FunctionFragment::kConditionalExpression);
  for (unsigned child = 0; child < 2; ++child) {
    Check(printed.functions[child].begin > printed.functions[2].begin);
    Check(printed.functions[child].end < printed.functions[2].end);
    Check(printed.members[2].expressions[child + 1] == printed.functions[child]);
  }
  if (argc > 1) { std::cout << output.str(); return 0; }
  auto default_family = family;
  auto& entry = default_family.members[2].tree;
  entry.names.push_back(u"$default$0");
  entry.symbols.push_back({{1}, {3}, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kSynthesized, {}});
  entry.expressions.push_back({TreeLiteral{IntegerLiteral{17}}, {0}});
  entry.statements.push_back({DeclarationStmt{{3}, ExprId{5}}, {0}});
  entry.children.insert(entry.children.begin(), {5});
  std::get<BlockStmt>(entry.statements[0].statement).child_count = 5;
  entry.module_scope = ScopeId{2}; entry.scopes.push_back({ScopeKind::kModule, {}});
  entry.scopes[0].parent = ScopeId{2};
  const ModuleDefaultExport default_export{{5}, {{9}, {7}}};
  entry.modules.push_back({{3}, default_export.binding, ModuleAccess::kMutable, true});
  std::ostringstream defaults;
  const auto default_print = PrintModuleFamilyDraft(default_family, defaults, {}, {}, nullptr, default_export);
  Check(std::holds_alternative<PrintedFamilyDraft>(default_print));
  Check(defaults.str().starts_with("export default 17;\n"));
  Check(defaults.str().find("let $default") == std::string::npos);
  Check(defaults.str().find("return state;") != std::string::npos);
  const auto range = std::get<PrintedFamilyDraft>(default_print).members[2].statements[5];
  Check(defaults.str().substr(range.begin, range.end - range.begin) == "export default 17;");
  auto invalid = family;
  invalid.members[2].tree.expressions[0].expression = ThisExpr{{10}};
  std::ostringstream denied;
  Check(std::get<TreePrintError>(PrintModuleFamilyDraft(invalid, denied)) == TreePrintError::kUnsupportedContext);
  invalid = family; invalid.members[2].tree.statements[1].statement = ReturnStmt{ExprId{0}};
  Check(std::get<TreePrintError>(PrintModuleFamilyDraft(invalid, denied)) == TreePrintError::kUnsupportedContext);
  invalid = family; invalid.members.erase(invalid.members.begin()); invalid.root = {1};
  Check(std::get<TreePrintError>(PrintModuleFamilyDraft(invalid, denied)) == TreePrintError::kUnsupportedContext);
  Check(std::get<TreePrintError>(PrintModuleFamilyDraft(family, denied, {0})) == TreePrintError::kOutputLimit);
}
