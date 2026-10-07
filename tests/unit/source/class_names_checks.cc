#include "class_names_checks.h"
#include "../../../src/source/class_names.h"
#include "../../../src/source/binding_initializer.h"
#include "../../../src/source/loop_syntax.h"
#include "../../../src/source/nullish_syntax.h"
#include <iostream>
#include <nlohmann/json.hpp>
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
  // A saved outer constructor can move into heritage. Class-self naming must
  // follow that actual occurrence, otherwise `class C extends C` adds a TDZ.
  std::uint64_t previous_projected_work = 0;
  for (const unsigned copies : {0U, 1U, 200U, 201U, 400U, 800U})
    for (const bool reverse : {false, true}) for (const bool parameter : {false, true})
      for (const bool conflict : {false, true}) {
    auto projected = Scope(kScopeOwnerBase + 3);
    projected.callable = CallableKind::kFunction;
    projected.strictness = Strictness::kStrict;
    projected.names = {u"saved", conflict ? u"C" : u"Other"};
    projected.symbols = {{{1}, {0}, BindingKind::kCompilerTemporary,
        DeclarationKind::kLet, NameEvidence::kSynthesized, TemporaryReason::kMultipleUses}};
    if (parameter) {
      projected.symbols.push_back({{0}, {1}, BindingKind::kParameter,
          DeclarationKind::kParameter, NameEvidence::kMetadata, {}});
      projected.parameters = {{{1}, {}, false}};
    }
    projected.origins = {OriginalSite{{0}, {0}}}; projected.body = {0};
    const auto expression = [&](TreeExpression value) {
      ExprId id{static_cast<std::uint32_t>(projected.expressions.size())};
      projected.expressions.push_back({std::move(value), {0}}); return id;
    };
    const auto statement = [&](TreeStatement value) {
      StmtId id{static_cast<std::uint32_t>(projected.statements.size())};
      projected.statements.push_back({std::move(value), {0}}); return id;
    };
    statement(BlockStmt{{1}, 0, 0});
    std::vector<StmtId> body{statement(DeclarationStmt{{0}, {}})};
    const auto source = parameter ? expression(SymbolExpr{{1}}) : expression(GlobalNameExpr{{1}});
    body.push_back(statement(ExpressionStmt{expression(AssignmentExpr{SymbolTarget{{0}}, source})}));
    SymbolId saved{0};
    for (unsigned i = 0; i < copies; ++i) {
      SymbolId next{static_cast<std::uint32_t>(projected.symbols.size())};
      NameId name{static_cast<std::uint32_t>(projected.names.size())};
      const auto text = "copy" + std::to_string(i); projected.names.emplace_back(text.begin(), text.end());
      projected.symbols.push_back({{1}, name, BindingKind::kCompilerTemporary,
          DeclarationKind::kLet, NameEvidence::kSynthesized, TemporaryReason::kMultipleUses});
      body.push_back(statement(DeclarationStmt{next, {}}));
      body.push_back(statement(ExpressionStmt{expression(AssignmentExpr{SymbolTarget{next}, expression(SymbolExpr{saved})})}));
      saved = next;
    }
    const auto read = expression(SymbolExpr{saved});
    body.push_back(statement(ReturnStmt{expression(ClassExpr{definition, read})}));
    projected.children = body;
    std::get<BlockStmt>(projected.statements[0].statement).child_count = body.size();
    if (reverse) {
      const auto remap = [&](StmtId id) {return StmtId{static_cast<std::uint32_t>(projected.statements.size() - 1 - id.value)};};
      for (auto& child : projected.children) child = remap(child);
      projected.body = remap(projected.body); std::reverse(projected.statements.begin(), projected.statements.end());
    }
    const auto loops = std::get<LoopSyntax>(PlanLoopSyntax(projected));
    const auto nullish = std::get<NullishSyntax>(PlanNullishSyntax(projected));
    const auto bindings = std::get<BindingInitializers>(PlanBindingInitializers(projected, loops, nullish));
    Check(parameter ? bindings.aliases[saved.value] == SymbolId{1} : bindings.replacements[read.value] == source);
    Check(Names(projected).Lookup(*definition).named);
    auto actual = PlanPrintedClassNames(projected, nullptr);
    Check(std::holds_alternative<ClassNames>(actual));
    const auto& names = std::get<ClassNames>(actual);
    Check(names.Lookup(*definition).named == !conflict);
    Check(std::get<TreeStructureError>(PlanPrintedClassNames(projected, nullptr, {names.allocated_bytes - 1, names.work})) == TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PlanPrintedClassNames(projected, nullptr, {names.allocated_bytes, names.work - 1})) == TreeStructureError::kWorkLimit);
    if (!reverse && !parameter && !conflict) {
      if (copies == 800) Check(names.work < previous_projected_work * 3);
      previous_projected_work = names.work;
    }
  }
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
  for (const auto kind : {DeclarationKind::kLet, DeclarationKind::kConst}) for (bool sequence : {false,true}) {
    auto syntax = Scope(); syntax.identity = definition->identity; syntax.method = definition->creation.creator;
    syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict; syntax.body = {0};
    Local(syntax); syntax.symbols[0].declaration = kind;
    syntax.origins.emplace_back(OriginalSite{{0},{0}});
    syntax.expressions = {{ClassExpr{definition,{}},{0}}, {SymbolExpr{{0}},{0}}};
    syntax.statements = {{BlockStmt{{1},0,2},{0}}, {DeclarationStmt{{0},ExprId{0}},{0}}, {ReturnStmt{ExprId{1}},{0}}};
    if(sequence) {
      syntax.expressions.push_back({TreeLiteral{IntegerLiteral{0}},{0}});
      syntax.expressions.push_back({SequenceExpr{ExprId{2},ExprId{0}},{0}});
      std::get<DeclarationStmt>(syntax.statements[1].statement).initializer=ExprId{3};
    }
    syntax.children = {{1},{2}};
    std::ostringstream output;
    const auto printed = PrintFunctionTree(syntax,output);
    Check(std::holds_alternative<PrintedFunction>(printed));
    Check(output.str().find(sequence ? kind == DeclarationKind::kLet ? "let C = (0, " : "const C = (0, " :
      kind == DeclarationKind::kLet ? "class C {" : "const C = class {") != std::string::npos);
    const auto& ranges = std::get<PrintedFunction>(printed);
    const auto statement = ranges.statements[1], expression = ranges.expressions[0];
    Check(statement.begin <= expression.begin && expression.begin < expression.end && expression.end <= statement.end);
  }
  for (const auto name : {u"C", u"other name"}) {
    definition->name = name;
    auto syntax = Scope(); syntax.identity = definition->identity; syntax.method = definition->creation.creator;
    syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict; syntax.body = {0};
    syntax.names = {u"saved"};
    syntax.symbols = {{{1},{0},BindingKind::kCompilerTemporary,DeclarationKind::kLet,
        NameEvidence::kSynthesized,TemporaryReason::kMultipleUses}};
    syntax.origins = {OriginalSite{{0},{0}}};
    syntax.expressions = {{ClassExpr{definition,{}},{0}},
        {AssignmentExpr{SymbolTarget{{0}},{0}},{0}}, {SymbolExpr{{0}},{0}}};
    syntax.statements = {{BlockStmt{{1},0,3},{0}}, {DeclarationStmt{{0},{}},{0}},
        {ExpressionStmt{{1}},{0}}, {ReturnStmt{ExprId{2}},{0}}};
    syntax.children = {{1},{2},{3}};
    std::ostringstream output;
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(syntax,output)));
    Check(output.str().find("saved") == std::string::npos);
    const std::u16string_view wanted{name};
    std::cout << nlohmann::json{{"snapshot_class",true},{"name",std::string(wanted.begin(),wanted.end())},
        {"source",output.str()}} << '\n';
  }
  definition->name = u"C";
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
