#include "../../../src/source/module_default_export.h"
#include "../../../src/source/function_family.h"

#include <cstdlib>
#include <iostream>
#include <source_location>
#include <sstream>

using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool ok, std::source_location at = std::source_location::current()) {
  if (!ok) { std::cerr << "check failed at " << at.line() << '\n'; std::abort(); }
}
constexpr ModuleDefaultExport kDefault{{1}, {{9}, {7}}};
FunctionTree Fixture(std::size_t count = 1) {
  FunctionTree root{};
  root.owner = {10}; root.callable = CallableKind::kFunction; root.strictness = Strictness::kStrict;
  root.parameter_scope = {0}; root.body_scope = {1}; root.body = {0};
  root.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
  root.module_scope = ScopeId{2}; root.scopes.push_back({ScopeKind::kModule, {}});
  root.scopes[0].parent = ScopeId{2};
  root.names = {u"$default$0"};
  root.symbols = {{{1}, {0}, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kSynthesized, {}}};
  root.modules = {{{0}, kDefault.binding, ModuleAccess::kMutable, true}};
  root.origins.emplace_back(OriginalSite{{0}, {0}});
  root.expressions = {{TreeLiteral{IntegerLiteral{42}}, {0}}, {TreeLiteral{UndefinedLiteral{}}, {0}}};
  root.statements = {{BlockStmt{{1}, 0, static_cast<std::uint32_t>(count + 1)}, {0}},
      {DeclarationStmt{{0}, ExprId{0}}, {0}}};
  root.children = {{1}};
  for (std::size_t i = 1; i < count; ++i) {
    root.children.push_back({static_cast<std::uint32_t>(root.statements.size())});
    root.expressions.push_back({TreeLiteral{IntegerLiteral{42}}, {0}});
    root.statements.push_back({ExpressionStmt{{static_cast<std::uint32_t>(root.expressions.size() - 1)}}, {0}});
  }
  root.children.push_back({static_cast<std::uint32_t>(root.statements.size())});
  root.statements.push_back({ReturnStmt{ExprId{1}}, {0}});
  return root;
}
bool Eligible(const FunctionTree& tree, ModuleDefaultExport candidate = kDefault) {
  const auto checked = CheckModuleDefaultExport(tree, nullptr, candidate);
  Check(std::holds_alternative<DefaultExportCheck>(checked));
  return std::get<DefaultExportCheck>(checked).eligible;
}
void Denied(const FunctionTree& tree, ModuleDefaultExport candidate = kDefault) {
  Check(!Eligible(tree, candidate));
  std::ostringstream output;
  Check(std::get<TreePrintError>(PrintModuleTree(tree, output, {}, {}, nullptr, candidate)) == TreePrintError::kUnsupportedContext);
  Check(output.str().empty());
}
}
int main() {
  auto root = Fixture();
  Check(Eligible(root));
  std::ostringstream output;
  const auto printed = PrintModuleTree(root, output, {}, {}, nullptr, kDefault);
  Check(std::holds_alternative<PrintedFunction>(printed));
  Check(output.str().starts_with("export default 42;\n"));
  const auto& positions = std::get<PrintedFunction>(printed);
  const auto statement = positions.statements[1], expression = positions.expressions[0];
  Check(output.str().substr(statement.begin, statement.end - statement.begin) == "export default 42;");
  Check(output.str().substr(expression.begin, expression.end - expression.begin) == "42");
  auto invalid = root; invalid.expressions[1].expression = SymbolExpr{{0}}; Denied(invalid);
  invalid = root; invalid.expressions[1].expression = AssignmentExpr{SymbolTarget{{0}}, {0}}; Denied(invalid);
  invalid = root; invalid.expressions[1].expression = UpdateExpr{{0}, UpdateDirection::kIncrement, UpdateResult::kPrefix}; Denied(invalid);
  invalid = root; invalid.symbols[0].evidence = NameEvidence::kMetadata; Denied(invalid);
  invalid = root; invalid.modules[0].access = ModuleAccess::kReadOnly; Denied(invalid);
  invalid = root; invalid.recovery_issues.push_back({RecoveryReason::kUnavailableValue, {}, {}}); Check(!Eligible(invalid));
  invalid = root; invalid.modules.push_back({{0}, {{9}, {8}}, ModuleAccess::kMutable, true}); Check(!Eligible(invalid));
  invalid = root; invalid.closure_captures.push_back({0}); Check(!Eligible(invalid));
  invalid = root; invalid.expressions[0].expression = FunctionExpr{{20}, {0, 0}}; Check(!Eligible(invalid));
  invalid = root; invalid.expressions[0].expression = ClassExpr{}; Check(!Eligible(invalid));
  invalid = root; invalid.statements.push_back(invalid.statements[1]); Check(!Eligible(invalid));
  Check(!Eligible(root, {{999}, kDefault.binding}));
  Check(!Eligible(root, {{1}, {{8}, {7}}}));
  Check(std::get<TreePrintError>(CheckModuleDefaultExport(root, nullptr, kDefault, {0})) == TreePrintError::kResourceLimit);
  Check(std::get<TreePrintError>(CheckModuleDefaultExport(root, nullptr, kDefault, {64 * 1024, 0})) == TreePrintError::kWorkLimit);
  // A sibling is a different symbol owner even when IDs coincide. Module/cell
  // targets, rather than its local ID or printed name, decide binding lifetime.
  auto child = Fixture(); child.owner = {20}; child.modules.clear();
  child.symbols[0].kind = BindingKind::kLocal;
  child.expressions[1].expression = SymbolExpr{{0}};
  ConstructedFamily family; family.root = {1}; family.members = {{std::move(child), {}, {}}, {root, {}, {}}};
  const auto eligible = [&] { return std::get<DefaultExportCheck>(CheckModuleDefaultExport(
      family.members[1].tree, &family, kDefault)).eligible; };
  Check(eligible());
  family.members[0].tree.modules = {{{0}, kDefault.binding, ModuleAccess::kMutable, false}};
  Check(!eligible()); family.members[0].tree.modules.clear();
  family.members[0].tree.captures = {{{0}, {{10}, {0}}}};
  Check(!eligible()); family.members[0].tree.captures.clear();
  family.members[0].tree.recovery_issues.push_back({RecoveryReason::kUnsupportedOperation, {}, {}});
  Check(!eligible()); family.members[0].tree.recovery_issues.clear();
  Check(!std::get<DefaultExportCheck>(CheckModuleDefaultExport(root, &family, kDefault)).eligible);
  std::uint64_t previous = 0;
  for (const auto count : {200U, 201U, 400U}) {
    const auto tree = Fixture(count);
    const auto checked = std::get<DefaultExportCheck>(CheckModuleDefaultExport(tree, nullptr, kDefault));
    Check(checked.eligible);
    if (previous) Check(checked.work < previous * 3);
    previous = checked.work;
    ConstructedFamily wide; wide.root = {count};
    for (unsigned i = 0; i < count; ++i) {
      auto member = Fixture(); member.modules.clear(); member.owner = {i + 100};
      wide.members.push_back({std::move(member), {}, {}});
    }
    wide.members.push_back({tree, {}, {}});
    const auto family_check = std::get<DefaultExportCheck>(CheckModuleDefaultExport(wide.members[count].tree, &wide, kDefault));
    Check(family_check.eligible);
    Check(family_check.work < checked.work * (count + 1) * 3);
    std::cout << count << ": " << checked.work << " family: " << family_check.work << '\n';
  }
}
