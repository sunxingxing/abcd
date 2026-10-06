#include "class_names_checks.h"
#include "../../../src/source/class_names.h"
#include <iostream>
#include <source_location>
#include <sstream>
namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool ok, std::source_location at = std::source_location::current()) {
  if (!ok) { std::cerr << "class name check " << at.line() << '\n'; std::abort(); }
}
// These are lexical-analysis fixtures, never source-publication evidence. Class
// constructor/accessor families come from the real decoded ABC fixture. Extra
// scope trees isolate captures/heritage and dense nesting without fake receipts.
constexpr std::uint32_t kScopeOwnerBase = 1u << 24;
FunctionTree Scope(std::uint32_t owner = kScopeOwnerBase) {
  FunctionTree tree{}; tree.owner = {owner}; tree.names = {u"C"};
  tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
  tree.parameter_scope = {0}; tree.body_scope = {1};
  return tree;
}
ClassNames Names(const FunctionTree& tree) {
  auto result = PlanClassNames(tree, nullptr);
  Check(std::holds_alternative<ClassNames>(result)); return std::get<ClassNames>(std::move(result));
}
void Local(FunctionTree& tree, BindingKind kind = BindingKind::kLocal) {
  tree.symbols.push_back({{1}, {0}, kind, DeclarationKind::kLet, NameEvidence::kSynthesized, {}});
}
void Static(ClassTree& tree, FunctionTree body, const std::function<FunctionContext()>& context) {
  tree.initializer_site.emplace();
  tree.initializer.emplace(ClassStaticFields{*tree.initializer_site, context(), std::move(body), {}});
}
}
void CheckClassNames(const std::function<std::shared_ptr<abcd::source::ClassTree>()>& create,
    const std::function<abcd::source::FunctionContext()>& context) {
  using namespace abcd; using namespace abcd::source;
  auto definition = create(); definition->name = u"C";
  auto root = Scope(); root.expressions = {{ClassExpr{definition, {}}, {}}};
  Check(Names(root).Lookup(*definition).named);
  // Heritage's reference is a child even when its arena entry precedes class.
  for (const bool reverse : {false, true}) {
    root.expressions = reverse ? std::vector<ExprNode>{{GlobalNameExpr{{0}}, {}}, {ClassExpr{definition, ExprId{0}}, {}}} :
        std::vector<ExprNode>{{ClassExpr{definition, ExprId{1}}, {}}, {GlobalNameExpr{{0}}, {}}};
    Check(!Names(root).Lookup(*definition).named);
    Local(root);
    root.expressions[reverse ? 0 : 1].expression = SymbolExpr{{0}};
    Check(!Names(root).Lookup(*definition).named); root.symbols.clear();
  }
  root.expressions = {{ClassExpr{definition, {}}, {}}, {GlobalNameExpr{{0}}, {}}};
  Check(Names(root).Lookup(*definition).named); // Outside class scope.
  auto body = Scope(kScopeOwnerBase + 1); Local(body);
  body.expressions = {{SymbolExpr{{0}}, {}}}; Static(*definition, body, context);
  Check(Names(root).Lookup(*definition).named); // Inner local shields name.
  body.modules = {{{0}, {{0}, {0}}, ModuleAccess::kMutable, true}};
  Static(*definition, body, context);
  Check(!Names(root).Lookup(*definition).named); // Module alias is external.
  body.modules.clear(); body.symbols[0].kind = BindingKind::kCapture;
  Local(root); body.captures = {{{0}, {root.owner, {0}}}};
  for (const bool write : {false, true}) {
    body.expressions = write ? std::vector<ExprNode>{{AssignmentExpr{SymbolTarget{{0}}, {1}}, {}},
        {TreeLiteral{IntegerLiteral{1}}, {}}} : std::vector<ExprNode>{{SymbolExpr{{0}}, {}}};
    Static(*definition, body, context); Check(!Names(root).Lookup(*definition).named);
  }
  body.captures.front().symbol.value = static_cast<std::uint32_t>(body.symbols.size());
  Static(*definition, body, context);
  Check(std::get<TreeStructureError>(PlanClassNames(root, nullptr)) == TreeStructureError::kRange);
  // Capture aliases through a closure in heritage must resolve to root storage.
  definition->initializer.reset(); definition->initializer_site.reset();
  auto child = Scope(kScopeOwnerBase + 2); Local(child, BindingKind::kCapture);
  child.captures = {{{0}, {root.owner, {0}}}}; child.expressions = {{SymbolExpr{{0}}, {}}};
  root.expressions = {{ClassExpr{definition, ExprId{1}}, {}}, {FunctionExpr{child.owner, {0, 1}}, {}}};
  root.closure_captures = {{0}};
  ConstructedFamily family; family.root = {1};
  family.members = {{child, {}, {}}, {root, {}, {}}};
  auto result = PlanClassNames(family, nullptr);
  Check(std::holds_alternative<ClassNames>(result) && !std::get<ClassNames>(result).Lookup(*definition).named);
  family.members[0].tree.captures.clear(); family.members[0].tree.symbols[0].kind = BindingKind::kParameter;
  result = PlanClassNames(family, nullptr);
  Check(std::holds_alternative<ClassNames>(result) && std::get<ClassNames>(result).Lookup(*definition).named);
  // Missing body cannot justify any added self scope.
  auto constructor = std::move(definition->constructor); definition->constructor.reset();
  root.expressions = {{ClassExpr{definition, {}}, {}}};
  Check(!Names(root).Lookup(*definition).named); definition->constructor = std::move(constructor);
  for (const auto name : {u"invalid name", u"eval", u"arguments", u"await", u"yield", u"\u540d\u79f0"}) {
    definition->name = name; Check(!Names(root).Lookup(*definition).named);
  }
  definition->name = u"C";
  // A native class declaration has writable outer storage. The same checked
  // class body in a const destination must retain the const initializer form.
  for (const auto kind : {DeclarationKind::kLet, DeclarationKind::kConst}) {
    auto syntax = Scope(); syntax.identity = definition->identity; syntax.method = definition->creation.creator;
    syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict; syntax.body = {0};
    Local(syntax); syntax.symbols[0].declaration = kind;
    syntax.origins.emplace_back(OriginalSite{{0},{0}});
    syntax.expressions = {{ClassExpr{definition,{}},{0}}, {SymbolExpr{{0}},{0}}};
    syntax.statements = {{BlockStmt{{1},0,2},{0}}, {DeclarationStmt{{0},ExprId{0}},{0}}, {ReturnStmt{ExprId{1}},{0}}};
    syntax.children = {{1},{2}};
    std::ostringstream output;
    const auto printed = PrintFunctionTree(syntax,output);
    Check(std::holds_alternative<PrintedFunction>(printed));
    Check(output.str().find(kind == DeclarationKind::kLet ? "class C {" : "const C = class {") != std::string::npos);
    const auto& ranges = std::get<PrintedFunction>(printed);
    const auto statement = ranges.statements[1], expression = ranges.expressions[0];
    Check(statement.begin <= expression.begin && expression.begin < expression.end && expression.end <= statement.end);
  }
  const auto measured = Names(root);
  Check(std::get<TreeStructureError>(PlanClassNames(root, nullptr, {measured.allocated_bytes - 1, measured.work})) ==
      TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(PlanClassNames(root, nullptr, {measured.allocated_bytes, measured.work - 1})) ==
      TreeStructureError::kWorkLimit);
  // Public class print reports planner scratch/work as well as body emission.
  std::ostringstream output;
  const auto planned = std::get<ClassNames>(PlanClassNames(*definition, nullptr));
  const auto printed = PrintClassTree(*definition, output);
  Check(std::holds_alternative<PrintedClassTree>(printed));
  Check(std::get<PrintedClassTree>(printed).work > planned.work &&
      std::get<PrintedClassTree>(printed).allocated_bytes >= planned.allocated_bytes);
  std::uint64_t previous_work = 0;
  for (const std::size_t count : {200, 201, 400, 800}) {
    std::vector<std::shared_ptr<ClassTree>> classes;
    for (std::size_t i = 0; i < count; ++i) { auto next = create(); next->name = u"C"; classes.push_back(std::move(next)); }
    for (std::size_t i = 0; i < count; ++i) {
      auto scope = Scope(kScopeOwnerBase + static_cast<std::uint32_t>(i + 1));
      scope.expressions = {{i + 1 == count ? TreeExpression{GlobalNameExpr{{0}}} :
          TreeExpression{ClassExpr{classes[i + 1], {}}}, {}}};
      Static(*classes[i], std::move(scope), context);
    }
    root.expressions = {{ClassExpr{classes.front(), {}}, {}}}; root.symbols.clear();
    const auto dense = Names(root);
    for (const auto& value : classes) Check(!dense.Lookup(*value).named);
    // A single terminal reference retires every same-name candidate once. The
    // work counter includes indexes/sorts; doubling must not approach n^2.
    if (previous_work) Check(dense.work < previous_work * 3);
    previous_work = dense.work;
    std::cout << "class names " << count << " work " << dense.work << '\n';
    // No conflicting use: all nested classes retain their own native names.
    classes.back()->initializer->tree.expressions[0].expression = TreeLiteral{IntegerLiteral{1}};
    const auto safe = Names(root);
    for (const auto& value : classes) Check(safe.Lookup(*value).named);
  }
}
