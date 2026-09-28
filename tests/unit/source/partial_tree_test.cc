#include "abcd/source/tree_printer.h"
#include "../../../src/source/tree_structure.h"
#include "../../../src/source/family_print.h"

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool condition, const char* message) {
  if (!condition) { std::cerr << message << '\n'; std::abort(); }
}
FunctionTree Tree() {
  FunctionTree tree{};
  tree.callable = CallableKind::kFunction;
  tree.strictness = Strictness::kStrict;
  tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {0};
  tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
  tree.origins = {EntryOrigin{}};
  tree.statements = {{BlockStmt{{1}, 0, 1}, {0}}, {DebuggerStmt{}, {0}}};
  tree.children = {{1}};
  return tree;
}
void DirectTree() {
  auto tree = Tree();
  std::ostringstream out;
  auto result = PrintFunctionTree(tree, out);
  Check(std::holds_alternative<PrintedFunction>(result), "plain AST must print without publication owner");
  Check(out.str().find("debugger;") != std::string::npos, "existing statement must be retained");
  Check(out.str().find("INCOMPLETE") == std::string::npos, "complete tree must not claim a gap");
}
void PartialTree() {
  auto tree = Tree();
  tree.recovery_issues.push_back({RecoveryReason::kUnsupportedControl, abc::CodeOffset{36}, abc::CodeOffset{72}});
  tree.statements.push_back({UnrecoveredStmt{{0}}, {0}});
  tree.children.push_back({2});
  std::get<BlockStmt>(tree.statements[0].statement).child_count = 2;
  std::ostringstream out;
  auto result = PrintFunctionTree(tree, out);
  Check(std::holds_alternative<PrintedFunction>(result), "gap must not reject known statements");
  const auto source = out.str();
  Check(source.find("INCOMPLETE") < source.find("function"), "mark incomplete before function");
  Check(source.find("debugger;") < source.find("UNRECOVERED"), "gap stays after known statement");
  Check(source.find("PC 36..72") != std::string::npos, "gap identifies original PC range");
  Check(source.find("return") == std::string::npos && source.find("throw") == std::string::npos,
        "no executable placeholder for a gap");
  const auto& printed = std::get<PrintedFunction>(result);
  Check(printed.incomplete, "output reports incomplete");
  Check(std::get<TreeFlowError>(VerifyTreeFlow({}, {}, {}, tree)) == TreeFlowError::kStructure,
        "partial syntax must not pass the existing whole-function semantic route");
  Check(source.substr(printed.statements[1].begin,
      printed.statements[1].end - printed.statements[1].begin) == "debugger;",
      "comment prefix must not break statement positions");
}
void InvalidTree() {
  auto tree = Tree(); tree.children[0] = {900};
  std::ostringstream out;
  Check(std::holds_alternative<TreePrintError>(PrintFunctionTree(tree, out)), "bad ID must fail");
  Check(out.str().empty(), "bad tree must fail before output");
  tree = Tree(); tree.statements[1].statement = UnrecoveredStmt{{99}};
  Check(std::holds_alternative<TreePrintError>(PrintFunctionTree(tree, out)), "bad issue ID must fail");
  tree = Tree();
  tree.recovery_issues.push_back({RecoveryReason::kUnavailableValue, abc::CodeOffset{8}, abc::CodeOffset{4}});
  Check(std::holds_alternative<TreePrintError>(PrintFunctionTree(tree, out)), "reversed PC range must fail");
  Check(out.str().empty(), "malformed gaps must fail before output");
}
void PartialFamily() {
  // Input order is deliberately unrelated to lexical nesting. The sibling
  // remains complete while the child gap marks both enclosing functions.
  auto root = Tree(); root.owner = {10};
  auto middle = Tree(); middle.owner = {20};
  auto child = Tree(); child.owner = {30};
  auto sibling = Tree(); sibling.owner = {40};
  root.expressions = {{FunctionExpr{{20}, {0, 0}}, {0}},
                      {FunctionExpr{{40}, {0, 0}}, {0}}};
  root.statements[1].statement = ExpressionStmt{{0}};
  root.statements.push_back({ExpressionStmt{{1}}, {0}});
  root.children.push_back({2});
  std::get<BlockStmt>(root.statements[0].statement).child_count = 2;
  middle.expressions = {{FunctionExpr{{30}, {0, 0}}, {0}}};
  middle.statements[1].statement = ReturnStmt{ExprId{0}};
  child.recovery_issues.push_back({RecoveryReason::kUnsupportedOperation, abc::CodeOffset{5}, {}});
  child.statements[1].statement = UnrecoveredStmt{{0}};
  ConstructedFamily family{};
  family.members = {{child, {}, {}}, {sibling, {}, {}}, {root, {}, {}}, {middle, {}, {}}};
  family.root = {2};
  std::ostringstream out;
  const auto result = PrintFamilyDraft(family, out);
  Check(std::holds_alternative<PrintedFamilyDraft>(result), "partial family must print");
  const auto& draft = std::get<PrintedFamilyDraft>(result);
  Check(draft.members[0].incomplete && draft.members[2].incomplete && draft.members[3].incomplete,
        "child gap propagates to all ancestors");
  Check(!draft.members[1].incomplete, "unrelated sibling stays complete");
  Check(out.str().starts_with("/* INCOMPLETE"), "root header identifies nested gaps");
  const auto returned = out.str().find("return ");
  const auto keyword = out.str().find("(function", returned);
  Check(returned != std::string::npos && keyword != std::string::npos &&
        out.str().substr(returned, keyword - returned).find('\n') == std::string::npos,
        "child annotation must not introduce automatic semicolon insertion after return");
}
}  // namespace
int main() { DirectTree(); PartialTree(); InvalidTree(); PartialFamily(); }
