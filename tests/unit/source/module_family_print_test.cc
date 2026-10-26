#include "../../../src/source/family_print.h"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <source_location>
#include <string_view>
#include <nlohmann/json.hpp>
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
enum class NamingPlace { kReturn, kDeclaration, kAssignment, kMember, kProperty, kArray, kConditional, kLogical, kSequence };
std::string_view CallableLabel(CallableKind kind) {
  switch (kind) {
    case CallableKind::kFunction: return "function";
    case CallableKind::kArrow: return "arrow";
    case CallableKind::kAsyncFunction: return "async-function";
    case CallableKind::kAsyncArrow: return "async-arrow";
    case CallableKind::kGenerator: return "generator";
    case CallableKind::kAsyncGenerator: return "async-generator";
    default: std::abort();
  }
}
ConstructedFamily Naming(CallableKind kind, NamingPlace place, std::string_view wanted, std::string_view key, bool sequence = false) {
  auto root = Base(10), child = Base(20);
  root.names = {u"chosen", std::u16string(key.begin(), key.end())};
  if (place == NamingPlace::kDeclaration || place == NamingPlace::kAssignment)
    root.symbols = {{{1}, {0}, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kSynthesized, {}}};
  root.expressions = {{FunctionExpr{{20}, {0,0}}, {0}}};
  const auto add = [&](TreeExpression node) {
    const ExprId id{static_cast<std::uint32_t>(root.expressions.size())}; root.expressions.push_back({std::move(node), {0}}); return id;
  };
  const ExprId created = sequence ? add(SequenceExpr{add(TreeLiteral{IntegerLiteral{0}}), ExprId{0}}) : ExprId{0};
  ExprId result=created;
  if (place == NamingPlace::kDeclaration || place == NamingPlace::kAssignment) {
    const auto initial = place == NamingPlace::kDeclaration ? created : add(TreeLiteral{UndefinedLiteral{}});
    root.statements.push_back({DeclarationStmt{{0}, initial}, {0}});
    root.children.insert(root.children.begin(), {2}); std::get<BlockStmt>(root.statements[0].statement).child_count = 2;
    result = place == NamingPlace::kDeclaration ? add(SymbolExpr{{0}}) : add(AssignmentExpr{SymbolTarget{{0}}, created});
  } else if (place == NamingPlace::kMember) {
    const auto object = add(EmptyObjectExpr{}); result = add(AssignmentExpr{MemberTarget{object, NameId{1}}, created});
  } else if (place == NamingPlace::kProperty || place == NamingPlace::kArray) {
    root.names.push_back(u"0");
    root.properties.push_back({place == NamingPlace::kArray ? NameId{2} : NameId{1}, created});
    const auto object = add(ObjectExpr{{0,1}, place == NamingPlace::kProperty ? AggregateKind::kObject : AggregateKind::kArray});
    result = add(MemberExpr{object, place == NamingPlace::kProperty ? MemberKey{NameId{1}} : MemberKey{ComputedKey{add(TreeLiteral{IntegerLiteral{0}})}}});
  } else if (place == NamingPlace::kConditional) {
    const auto condition = add(TreeLiteral{BooleanLiteral{true}});
    const auto alternate = add(TreeLiteral{UndefinedLiteral{}}); result = add(ConditionalExpr{condition, created, alternate});
  } else if (place == NamingPlace::kLogical) {
    const auto condition = add(TreeLiteral{BooleanLiteral{true}}); result = add(LogicalExpr{TreeLogical::kAnd, condition, created});
  } else if (place == NamingPlace::kSequence) {
    const auto first = add(TreeLiteral{IntegerLiteral{1}}); result = add(SequenceExpr{first, created});
  }
  root.statements[1].statement = ReturnStmt{result};
  child.callable = kind; child.names = {u"arg", std::u16string(wanted.begin(), wanted.end())}; child.inferred_name = NameId{1};
  child.symbols = {{{0}, {0}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}}};
  child.parameters = {{{0}, {}, false}}; child.expressions = {{SymbolExpr{{0}}, {0}}};
  if (IsGeneratorCallable(kind)) {
    child.expressions.push_back({YieldExpr{{0}}, {0}}); child.statements[1].statement = ReturnStmt{ExprId{1}};
  } else if (IsAsyncCallable(kind)) {
    child.expressions.push_back({AwaitExpr{{0}}, {0}}); child.statements[1].statement = ReturnStmt{ExprId{1}};
  }
  ConstructedFamily family; family.root = {1};
  family.members = {{std::move(child), {}, {}}, {std::move(root), {}, {}}}; return family;
}
void PropertyNames() {
  for (const auto kind : {CallableKind::kFunction, CallableKind::kArrow})
  for (const auto key : {"foo", "42", "", "__proto__", "a.\\name"}) {
    auto family = Naming(kind, NamingPlace::kProperty, "", key);
    family.members[1].tree.properties[0].name_setting = analysis::PropertyNameSetting::kPropertyKey;
    std::ostringstream output;
    Check(std::holds_alternative<PrintedFamilyDraft>(PrintFamilyDraft(family, output)));
    std::cout << nlohmann::json{{"source", output.str()}, {"key", key}}.dump() << '\n';
    auto renamed = family; renamed.members[0].tree.names[1] = u"explicit";
    std::ostringstream denied;
    Check(std::holds_alternative<TreePrintFailure>(PrintFamilyDraft(renamed, denied)));
    auto shared = family;
    shared.members[1].tree.expressions[0].expression = TreeLiteral{IntegerLiteral{1}};
    Check(std::holds_alternative<TreePrintFailure>(PrintFamilyDraft(shared, denied)));
  }
}
void Snapshot(ConstructedFamily& family) {
  auto& root = family.members[family.root.value].tree;
  const NameId name{static_cast<std::uint32_t>(root.names.size())}; root.names.push_back(u"saved");
  const SymbolId symbol{static_cast<std::uint32_t>(root.symbols.size())};
  root.symbols.push_back({{1},name,BindingKind::kCompilerTemporary,DeclarationKind::kLet,
      NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
  const ExprId created{static_cast<std::uint32_t>(root.expressions.size())};
  root.expressions.push_back(root.expressions[0]); root.expressions[0].expression=SymbolExpr{symbol};
  const ExprId store{static_cast<std::uint32_t>(root.expressions.size())};
  root.expressions.push_back({AssignmentExpr{SymbolTarget{symbol},created},{0}});
  const StmtId declaration{static_cast<std::uint32_t>(root.statements.size())};
  root.statements.push_back({DeclarationStmt{symbol,{}},{0}});
  const StmtId assignment{static_cast<std::uint32_t>(root.statements.size())};
  root.statements.push_back({ExpressionStmt{store},{0}});
  root.children.insert(root.children.end()-1,{declaration,assignment});
  std::get<BlockStmt>(root.statements[0].statement).child_count+=2;
}
void SnapshotNames() {
  for (const auto kind : {CallableKind::kFunction,CallableKind::kArrow,CallableKind::kAsyncFunction,
      CallableKind::kAsyncArrow,CallableKind::kGenerator,CallableKind::kAsyncGenerator})
    for (const auto place : {NamingPlace::kReturn,NamingPlace::kAssignment,NamingPlace::kProperty,
        NamingPlace::kArray,NamingPlace::kSequence})
      for (const auto wanted : {"","saved","chosen","key","other name"}) {
    auto family=Naming(kind,place,wanted,"key"); Snapshot(family);
    std::ostringstream output;
    const auto printed=PrintFamilyDraft(family,output);
    Check(std::holds_alternative<PrintedFamilyDraft>(printed));
    Check(output.str().find("const saved") == std::string::npos);
    Check(output.str().find("let saved") == std::string::npos);
    std::cout << nlohmann::json{{"callable",CallableLabel(kind)},{"place",static_cast<unsigned>(place)},
        {"wanted",wanted},{"source",output.str()}} << '\n';
    for(auto& member:family.members)member.tree.identity.contract=SourceContract::kLanguageSourceV5;
    std::ostringstream strict;
    const auto constrained=PrintFamilyDraft(family,strict);
    Check(std::holds_alternative<PrintedFamilyDraft>(constrained));
    Check(strict.str() == output.str());
  }
}
void Names() {
  for (const auto kind : {CallableKind::kFunction, CallableKind::kArrow, CallableKind::kAsyncFunction,
      CallableKind::kAsyncArrow, CallableKind::kGenerator, CallableKind::kAsyncGenerator})
    for (const auto place : {NamingPlace::kReturn, NamingPlace::kDeclaration, NamingPlace::kAssignment,
        NamingPlace::kMember, NamingPlace::kProperty, NamingPlace::kArray, NamingPlace::kConditional,
        NamingPlace::kLogical, NamingPlace::kSequence})
      for (const auto key : {"key", "__proto__", "line\nname"})
        for (const auto wanted : {"", "chosen", key, "other name"}) for (bool sequence : {false,true}) {
          auto family = Naming(kind, place, wanted, key, sequence);
          std::ostringstream output;
          const auto printed = PrintFamilyDraft(family, output);
          if (const auto* error = std::get_if<TreePrintFailure>(&printed))
            std::cerr << "name print error " << unsigned(*error) << " kind " << unsigned(kind)
                      << " place " << unsigned(place) << " name " << wanted << '\n';
          Check(std::holds_alternative<PrintedFamilyDraft>(printed));
          const std::string_view context = sequence ? "" : place == NamingPlace::kDeclaration || place == NamingPlace::kAssignment ? "chosen" :
              place == NamingPlace::kProperty ? key : "";
          const bool native = context == wanted;
          const auto& ranges = std::get<PrintedFamilyDraft>(printed);
          const auto at = ranges.functions[0];
          const auto child = output.str().substr(at.begin, at.end - at.begin);
          Check((child.find("{[") == std::string::npos) == native);
          std::cout << nlohmann::json{{"callable", CallableLabel(kind)}, {"place", static_cast<unsigned>(place)},
              {"wanted", wanted}, {"key", key}, {"native", native}, {"sequence",sequence}, {"source", output.str()}} << '\n';
          // Historical Source identities share the same naming/resource premise.
          for (auto& member : family.members) member.tree.identity.contract = SourceContract::kLanguageSourceV5;
          std::ostringstream constrained;
          Check(std::holds_alternative<PrintedFamilyDraft>(PrintFamilyDraft(family, constrained)));
          Check(constrained.str() == output.str());
        }
}
void DefaultNames() {
  for (const auto kind : {CallableKind::kFunction,CallableKind::kArrow,CallableKind::kAsyncFunction,
      CallableKind::kAsyncArrow,CallableKind::kGenerator,CallableKind::kAsyncGenerator})
    for (const auto wanted : {"default","","chosen","other name"}) {
      auto family = Naming(kind,NamingPlace::kDeclaration,wanted,"key");
      auto& root = family.members[family.root.value].tree;
      root.names[0] = u"$default$0";
      root.module_scope = ScopeId{2}; root.scopes.push_back({ScopeKind::kModule,{}});
      root.scopes[0].parent = ScopeId{2};
      root.modules.push_back({{0},{{9},{7}},ModuleAccess::kMutable,true});
      root.expressions[1].expression = TreeLiteral{UndefinedLiteral{}};
      const ModuleDefaultExport default_export{{2},{{9},{7}}};
      std::ostringstream output;
      const auto printed = PrintModuleFamilyDraft(family,output,{}, {},nullptr,default_export);
      Check(std::holds_alternative<PrintedFamilyDraft>(printed));
      Check(output.str().starts_with("export default ("));
      Check(output.str().find("let $default") == std::string::npos);
      const auto& ranges = std::get<PrintedFamilyDraft>(printed);
      const auto child = ranges.functions[0];
      Check((output.str().substr(child.begin,child.end-child.begin).find("({[") == std::string::npos) ==
          (std::string_view(wanted) == "default"));
      for (auto& member : family.members) member.tree.identity.contract = SourceContract::kLanguageSourceV5;
      std::ostringstream strict_output;
      const auto strict = PrintModuleFamilyDraft(family,strict_output,{}, {},nullptr,default_export);
      Check(std::holds_alternative<PrintedFamilyDraft>(strict));
      Check(strict_output.str() == output.str());
      std::cout << nlohmann::json{{"callable",CallableLabel(kind)},{"wanted",wanted},{"source",output.str()}} << '\n';
    }
}
void NameShadow() {
  auto family = Naming(CallableKind::kFunction, NamingPlace::kDeclaration, "chosen", "key");
  auto& root = family.members[1].tree;
  auto& child = family.members[0].tree;
  root.names.push_back(u"saved");
  root.symbols.push_back({{1}, {2}, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kSynthesized, {}});
  root.closure_captures = {{0}};
  std::get<FunctionExpr>(root.expressions[0].expression).captures = {0,1};
  root.expressions.push_back({TreeLiteral{IntegerLiteral{42}}, {0}});
  root.expressions.push_back({AssignmentExpr{SymbolTarget{{0}}, {2}}, {0}});
  root.expressions.push_back({SymbolExpr{{1}}, {0}});
  root.statements = {{BlockStmt{{1}, 0, 4}, {0}}, {DeclarationStmt{{0}, ExprId{0}}, {0}},
      {DeclarationStmt{{1}, ExprId{1}}, {0}}, {ExpressionStmt{{3}}, {0}}, {ReturnStmt{ExprId{4}}, {0}}};
  root.children = {{1}, {2}, {3}, {4}};
  child.capture_scope = ScopeId{2}; child.scopes.push_back({ScopeKind::kCaptures, {}});
  child.scopes[0].parent = ScopeId{2};
  child.symbols.push_back({{2}, {1}, BindingKind::kCapture, DeclarationKind::kLet, NameEvidence::kSynthesized, {}});
  child.captures = {{{1}, {{10}, {0}}}};
  child.expressions[0].expression = SymbolExpr{{1}};
  std::ostringstream output;
  Check(std::holds_alternative<PrintedFamilyDraft>(PrintFamilyDraft(family, output)));
  Check(output.str().find("({[") == std::string::npos);
  std::cout << output.str();
}
void NameScale() {
  for (const unsigned count : {1, 200, 201, 402}) {
    ConstructedFamily family; family.root = {0};
    family.members.resize(count + 1);
    auto root = Base(10);
    for (unsigned i = 0; i < count; ++i) {
      const auto index = std::to_string(i);
      root.names.emplace_back(index.begin(), index.end());
      const FunctionOwnerId owner{i + 100};
      root.expressions.push_back({FunctionExpr{owner, {0,0}}, {0}});
      root.properties.push_back({NameId{i}, {i}});
      auto child = Naming(CallableKind::kFunction, NamingPlace::kReturn, "", "key").members[0].tree;
      child.owner = owner; child.identity.contract = SourceContract::kLanguageSourceV5;
      family.members[i + 1].tree = std::move(child);
    }
    root.identity.contract = SourceContract::kLanguageSourceV5;
    root.expressions.push_back({ObjectExpr{{0,count}, AggregateKind::kArray}, {0}});
    root.statements[1].statement = ReturnStmt{ExprId{count}};
    family.members[0].tree = std::move(root);
    std::ostringstream output;
    const auto printed = PrintFamilyDraft(family, output);
    Check(std::holds_alternative<PrintedFamilyDraft>(printed));
    const auto& ranges = std::get<PrintedFamilyDraft>(printed);
    Check(ranges.functions.size() == count + 1 && output.str().find("({[") == std::string::npos);
    analysis::SemanticBudget exact; exact.work = ranges.work;
    std::ostringstream repeated;
    Check(std::holds_alternative<PrintedFamilyDraft>(PrintFamilyDraft(family, repeated, {}, exact)));
    Check(repeated.str() == output.str());
    --exact.work; std::ostringstream insufficient;
    Check(std::get<TreePrintFailure>(PrintFamilyDraft(family, insufficient, {}, exact)) == TreePrintError::kWorkLimit);
    std::cout << nlohmann::json{{"count", count}, {"work", ranges.work},
        {"allocated", ranges.allocated_bytes}, {"source", output.str()}} << '\n';
  }
}
enum class TailCase { kBare, kLiteral, kValue, kCall, kSequence, kEarly, kNested, kFinallyReturn, kSuspend };
std::string_view TailLabel(TailCase kind) {
  switch (kind) {
    case TailCase::kBare: return "bare";
    case TailCase::kLiteral: return "literal";
    case TailCase::kValue: return "value";
    case TailCase::kCall: return "call";
    case TailCase::kSequence: return "sequence";
    case TailCase::kEarly: return "early";
    case TailCase::kNested: return "nested";
    case TailCase::kFinallyReturn: return "finally-return";
    case TailCase::kSuspend: return "suspend";
  }
  std::abort();
}
void TailCompletions() {
  for (const auto callable : {CallableKind::kFunction, CallableKind::kArrow, CallableKind::kAsyncFunction,
      CallableKind::kAsyncArrow, CallableKind::kGenerator, CallableKind::kAsyncGenerator})
    for (const auto kind : {TailCase::kBare, TailCase::kLiteral, TailCase::kValue, TailCase::kCall,
        TailCase::kSequence, TailCase::kEarly, TailCase::kNested, TailCase::kFinallyReturn, TailCase::kSuspend}) {
      auto tree = Base(10); tree.callable = callable;
      tree.names = {u"event", u"value"};
      tree.symbols = {{{0}, {0}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}},
          {{0}, {1}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}}};
      tree.parameters = {{{0}, {}, false}, {{1}, {}, false}};
      tree.expressions = {{TreeLiteral{UndefinedLiteral{}}, {0}}};
      const auto add = [&](TreeExpression expression) {
        const ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};
        tree.expressions.push_back({std::move(expression), {0}}); return id;
      };
      const auto event = [&] { const auto callee = add(SymbolExpr{{0}}); return add(CallExpr{callee, {0,0}, CallReference::kValue}); };
      const auto value = [&] { return add(SymbolExpr{{1}}); };
      bool omitted = kind == TailCase::kBare || kind == TailCase::kLiteral || kind == TailCase::kSuspend;
      if (callable == CallableKind::kAsyncGenerator && kind != TailCase::kBare) omitted = false;
      if (kind == TailCase::kBare) { tree.expressions.clear(); tree.statements[1].statement = ReturnStmt{}; }
      if (kind == TailCase::kValue) { tree.expressions.clear(); tree.statements[1].statement = ReturnStmt{value()}; }
      if (kind == TailCase::kCall) { tree.expressions.clear(); tree.statements[1].statement = ReturnStmt{event()}; }
      if (kind == TailCase::kSequence) tree.statements[1].statement = ReturnStmt{add(SequenceExpr{event(), {0}})};
      if (kind == TailCase::kEarly || kind == TailCase::kNested) {
        tree.statements.push_back({ExpressionStmt{event()}, {0}});
        tree.children.push_back({2}); std::get<BlockStmt>(tree.statements[0].statement).child_count = 2;
        if (kind == TailCase::kNested) {
          tree.scopes.push_back({ScopeKind::kBlock, tree.body_scope});
          tree.statements.push_back({BlockStmt{{2}, 2, 1}, {0}});
          tree.children[0] = {3}; tree.children.push_back({1});
        }
      }
      if (kind == TailCase::kFinallyReturn) {
        tree.scopes.push_back({ScopeKind::kBlock, tree.body_scope});
        tree.scopes.push_back({ScopeKind::kBlock, tree.body_scope});
        tree.statements = {{BlockStmt{{1}, 0, 1}, {0}}, {TryStmt{{2}, {}, StmtId{4}}, {0}},
            {BlockStmt{{2}, 1, 1}, {0}}, {ReturnStmt{value()}, {0}},
            {BlockStmt{{3}, 2, 1}, {0}}, {ReturnStmt{ExprId{0}}, {0}}};
        tree.children = {{1}, {3}, {5}};
      }
      if (kind == TailCase::kSuspend) {
        const auto operand = value();
        const auto suspended = IsGeneratorCallable(callable) ? add(YieldExpr{operand}) :
            IsAsyncCallable(callable) ? add(AwaitExpr{operand}) : operand;
        tree.statements.push_back({ExpressionStmt{suspended}, {0}});
        tree.children = {{2}, {1}}; std::get<BlockStmt>(tree.statements[0].statement).child_count = 2;
      }
      std::ostringstream output;
      const auto printed = PrintFunctionTree(tree, output);
      Check(std::holds_alternative<PrintedFunction>(printed));
      const auto& positions = std::get<PrintedFunction>(printed);
      if (omitted) {
        Check(output.str().find("return") == std::string::npos);
        Check(positions.statements[1].begin == positions.statements[1].end);
        const auto block = positions.statements[0];
        Check(block.begin <= positions.statements[1].begin && positions.statements[1].end < block.end);
        if (kind != TailCase::kBare) Check(positions.expressions[0] == positions.statements[1]);
      } else Check(output.str().find("return") != std::string::npos);
      std::cout << nlohmann::json{{"callable", CallableLabel(callable)}, {"case", TailLabel(kind)},
          {"omitted", omitted}, {"source", output.str()}} << '\n';
    }
}
}
int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--tail-completions") { TailCompletions(); return 0; }
  if (argc == 2 && std::string_view(argv[1]) == "--snapshot-names") { SnapshotNames(); return 0; }
  if (argc == 2 && std::string_view(argv[1]) == "--property-names") { PropertyNames(); return 0; }
  if (argc == 2 && std::string_view(argv[1]) == "--names") { Names(); return 0; }
  if (argc == 2 && std::string_view(argv[1]) == "--default-names") { DefaultNames(); return 0; }
  if (argc == 2 && std::string_view(argv[1]) == "--name-shadow") { NameShadow(); return 0; }
  if (argc == 2 && std::string_view(argv[1]) == "--name-scale") { NameScale(); return 0; }
  auto family = Fixture();
  const std::string_view mode = argc > 1 ? argv[1] : "";
  auto& receiver = family.members[1].tree;
  if (mode == "--undefined-result") {
    auto& root = family.members[2].tree;
    root.expressions.push_back({TreeLiteral{UndefinedLiteral{}}, {0}});
    root.expressions.push_back({SequenceExpr{{4},{5}}, {0}});
    root.statements[4].statement = ReturnStmt{ExprId{6}};
  }
  if (mode == "--generator" || mode == "--async") {
    receiver.callable = mode == "--generator" ? CallableKind::kGenerator : CallableKind::kAsyncFunction;
    receiver.expressions.push_back({mode == "--generator" ? TreeExpression{YieldExpr{{0}}} :
        TreeExpression{AwaitExpr{{0}}}, {0}});
    receiver.statements[1].statement = ReturnStmt{ExprId{1}};
  }
  std::ostringstream output;
  const auto result = PrintModuleFamilyDraft(family, output);
  if (const auto* e = std::get_if<TreePrintFailure>(&result)) std::cerr << "print error " << unsigned(*e) << '\n';
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
  {
    auto empty = Base(99);
    empty.expressions = {{TreeLiteral{UndefinedLiteral{}}, {0}}};
    ConstructedFamily completion; completion.root = {0};
    completion.members = {{std::move(empty), {}, {}}};
    std::ostringstream omitted;
    const auto ended = PrintModuleFamilyDraft(completion, omitted);
    Check(std::holds_alternative<PrintedFamilyDraft>(ended));
    Check(omitted.str() == "\n");
    const auto& ranges = std::get<PrintedFamilyDraft>(ended);
    Check(ranges.members[0].statements[1] == GeneratedRange{0,0});
    Check(ranges.members[0].expressions[0] == GeneratedRange{0,0});
    std::ostringstream ordinary;
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(completion.members[0].tree,ordinary)));
    Check(ordinary.str().find("return") == std::string::npos);
  }
  auto invalid = family;
  invalid.members[2].tree.expressions[0].expression = ThisExpr{{10}};
  std::ostringstream denied;
  Check(std::get<TreePrintFailure>(PrintModuleFamilyDraft(invalid, denied)) == TreePrintError::kUnsupportedContext);
  invalid = family; invalid.members[2].tree.statements[1].statement = ReturnStmt{ExprId{0}};
  Check(std::get<TreePrintFailure>(PrintModuleFamilyDraft(invalid, denied)) == TreePrintError::kUnsupportedContext);
  invalid = family; invalid.members.erase(invalid.members.begin()); invalid.root = {1};
  Check(std::get<TreePrintFailure>(PrintModuleFamilyDraft(invalid, denied)) == TreePrintError::kUnsupportedContext);
  Check(std::get<TreePrintFailure>(PrintModuleFamilyDraft(family, denied, {0})) == TreePrintError::kOutputLimit);
}
