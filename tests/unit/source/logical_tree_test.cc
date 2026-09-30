#include "../../../src/source/tree_structure.h"
#include "../../../src/source/family_print.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string_view>

namespace {
using namespace abcd::source;
void Check(bool condition) { if (!condition) std::abort(); }
struct Fixture {
  FunctionTree tree{};
  Fixture() {
    tree.callable = CallableKind::kFunction;
    tree.strictness = Strictness::kStrict;
    tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {0};
    tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
    tree.origins.emplace_back(OriginalSite{{0}, {0}});
    tree.statements.push_back({BlockStmt{{1}, 0, 1}, {0}});
    tree.statements.push_back({ReturnStmt{}, {0}});
    tree.children.push_back({1});
  }
  ExprId Add(TreeExpression expression) {
    ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};
    tree.expressions.push_back({std::move(expression), {0}}); return id;
  }
  NameId Name(std::u16string text) {
    NameId id{static_cast<std::uint32_t>(tree.names.size())};
    tree.names.push_back(std::move(text)); return id;
  }
  ExprId Global(std::u16string text) { return Add(GlobalNameExpr{Name(std::move(text))}); }
  ExprId Invoke(std::u16string text) {
    return Add(CallExpr{Global(std::move(text)), {0, 0}, CallReference::kValue});
  }
  ExprId Logical(TreeLogical op, ExprId left, ExprId right) { return Add(LogicalExpr{op, left, right}); }
  void Return(ExprId expression) { tree.statements[1].statement = ReturnStmt{expression}; }
};
std::string Print(const FunctionTree& tree) {
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto family = std::get<ConstructedFamily>(BuildFunctionFamily({{tree, {}, {}}}, tree.owner));
  std::ostringstream output;
  Check(std::holds_alternative<PrintedFamilyDraft>(PrintFamilyDraft(family, output)));
  return output.str();
}
// Syntax fixtures exercise the common tree/printer only. They do not certify
// any CFG projection or permission to fuse a member read across a branch.
FunctionTree MemberArgument(unsigned shape) {
  Fixture f;
  const auto callee = f.Add(MemberExpr{f.Global(u"target"), f.Name(u"method")});
  const auto value = f.Add(MemberExpr{f.Global(u"subject"), f.Name(u"value")});
  const auto fallback = f.Invoke(u"fallback");
  ExprId argument{};
  if (shape == 0) argument = f.Logical(TreeLogical::kNullish, value, fallback);
  else if (shape <= 2) {
    const auto inner = f.Logical(TreeLogical::kNullish, value, fallback);
    argument = f.Logical(shape == 1 ? TreeLogical::kOr : TreeLogical::kAnd, inner, f.Invoke(u"extra"));
  } else {
    const auto inner = f.Logical(shape == 3 ? TreeLogical::kOr : TreeLogical::kAnd, fallback, f.Invoke(u"extra"));
    argument = f.Logical(TreeLogical::kNullish, value, inner);
  }
  f.tree.arguments.push_back(argument);
  f.Return(f.Add(CallExpr{callee, {0, 1}, CallReference::kMember}));
  return f.tree;
}
void Structure() {
  for (const unsigned count : {1U, 200U, 201U, 402U}) {
    Fixture f;
    auto value = f.Add(TreeLiteral{NullLiteral{}});
    for (unsigned i = 0; i < count; ++i)
      value = f.Logical(TreeLogical::kNullish, value, f.Add(TreeLiteral{UndefinedLiteral{}}));
    f.Return(value);
    const auto text = Print(f.tree);
    std::size_t found = 0, offset = 0;
    while ((offset = text.find(" ?? ", offset)) != std::string::npos) { ++found; offset += 4; }
    Check(found == count);
    auto invalid = f.tree;
    std::get<LogicalExpr>(invalid.expressions.back().expression).operation = static_cast<TreeLogical>(255);
    Check(std::get<TreeStructureError>(VerifyTreeStructure(invalid)) == TreeStructureError::kUnsupportedSyntax);
    invalid = f.tree;
    auto& node = std::get<LogicalExpr>(invalid.expressions.back().expression);
    node.right = node.left;
    Check(std::get<TreeStructureError>(VerifyTreeStructure(invalid)) == TreeStructureError::kOwnership);
  }
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--sources") {
    std::cout << "[\n";
    for (unsigned shape = 0; shape < 5; ++shape) {
      if (shape) std::cout << ",\n";
      std::cout << Print(MemberArgument(shape));
    }
    std::cout << "]\n"; return 0;
  }
  Structure();
  for (unsigned shape = 0; shape < 5; ++shape) Check(!Print(MemberArgument(shape)).empty());
}
