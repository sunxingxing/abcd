#include "sendable_class.h"
#include "concurrent_references.h"
#include "concurrent_print.h"
#include "tree_print_trace.h"
#include "source_variables.h"
#include "regexp_literal.h"
#include "template_literal.h"
#include "tail_completion.h"
#include "discarded_read.h"
#include "class_names.h"
#include "../analysis/register_value_rule.h"
#include "nullish_syntax.h"
#include "loop_syntax.h"
#include "binding_initializer.h"
#include "expression_children.h"
#include "statement_children.h"
#include "statement_expressions.h"
#include "abcd/source/tree_printer.h"
#include "abcd/source/module_enclosure.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>
#include <new>
#include <ostream>
#include <stdexcept>

#include "abcd/source/scalar_expression.h"
#include "abcd/source/string_literal.h"
#include "tree_print_tokens.h"
#include "grouping_plan.h"
#include "tree_globals.h"
#include "family_print.h"
#include "class_tree.h"
#include "family_enclosure.h"
#include "family_route_failure.h"
#include "class_tree_context.h"
#include "tree_checks.h"
#include "switch_syntax.h"
#include "member_key.h"
#include "module_default_export.h"

namespace abcd::source {
namespace {
using Error = TreePrintError;
enum class ClassPrintForm { kExpression, kInferredExpression, kNamedExpression, kBareNamedExpression, kDeclaration, kDerivedBody };
std::variant<PrintedClassTree, TreePrintFailure, FamilyPrepareFailure> PrintClassPart(
    const ClassTree&, std::ostream&, EmitBudget, analysis::SemanticBudget,
    const GlobalScope*, std::size_t, ClassPrintForm, const ClassCaptureDestination* = nullptr,
    const ClassNames* = nullptr);
struct DerivedClassBody { std::shared_ptr<const ClassTree> definition; ExprId expression; ClassPrintForm form; };
struct EndExpression { ExprId id; };
struct ProjectedExpression { ExprId id; std::uint64_t begin, end; GeneratedExtent extent = GeneratedExtent::kSpan; std::optional<ExprId> retained; };
struct ProjectElidedOperands { ExprId expression, retained; };
struct NoInferredName {};
struct DefaultInferredName {};
struct PropertyInferredName { NameId key; };
using NameContext = std::variant<NoInferredName, NameId, DefaultInferredName, PropertyInferredName>;
enum class ExpressionObservation : std::uint8_t { kValue, kTruthiness, kDiscarded };
struct ExpressionOperand {
  ExprId id;
  TreeOperatorContext context;
  NameContext inferred_name = {};
  ExpressionObservation observation = ExpressionObservation::kValue;
  bool value_result = false;
  bool statement_start = false;
};
// Composition of language Boolean results, not a bytecode recovery pattern.
// Keep the original parent grammar until a remaining conversion is emitted.
struct BooleanOperand { ExpressionOperand expression; bool inverted; };
struct PostfixSpine { ExprId expression; ExprId access; ExprId base; };
enum class BlockForm : std::uint8_t { kBraced, kList, kElseIf };
struct Statement { StmtId id; std::size_t depth; BlockForm block = BlockForm::kBraced; };
struct EndStatement { StmtId id; };
struct LoopHeader { StmtId id; std::optional<StmtId> retained; bool discarded = false; };
struct LoopHeaderCursor { StmtId block; std::uint32_t index; std::uint32_t count; std::optional<StmtId> retained; bool discarded = false; };
struct LoopTransitionEnd { StmtId block; std::uint32_t first; };
struct LoopInitializerCursor { std::uint32_t index; std::uint32_t end; bool emitted = false; };
struct LoopDeclarationCursor { std::uint32_t index; std::uint32_t end; bool emitted = false; };
struct LoopKeyword { std::optional<LabelId> label; };
struct ElideStatement { StmtId id; };
struct LoopBody { StmtId block; LoopConditionSyntax syntax; std::size_t depth; };
struct BlockCursor { StmtId id; std::uint32_t index; std::size_t depth; BlockForm form = BlockForm::kBraced; };
struct Indent { std::size_t depth; };
struct PropertyKey { NameId id; bool optional = false; };
struct SymbolName { SymbolId id; };
struct ObjectRestCursor { const ObjectRestSurface* pattern; std::uint32_t index; };
struct TemplateChunk { ExprId object; std::uint32_t index; };
struct TemplateStart { ExprId object; };
struct ArgumentCursor { ArgumentRange range; std::uint32_t index; };
struct ArrayArgumentCursor { ExprId array; };
struct SnapshotCursor { ArgumentRange range; std::uint32_t index; };
struct ObjectCursor {
  PropertyRange range;
  std::uint32_t index;
  AggregateKind kind = AggregateKind::kObject;
  bool multiline = false;
};
struct EndProperty { PropertyId id; };
struct BeginFunction { bool grouped = true; };
struct EndFunction { bool grouped = true; };
struct ParameterCursor { std::uint32_t index; };
struct EndParameter { std::uint32_t index; };
using PrintAction = std::variant<TemplateStart, TemplateChunk, std::string_view, ExprId, EndExpression, ProjectedExpression, ProjectElidedOperands, ExpressionOperand, BooleanOperand, PostfixSpine,
    Statement, EndStatement, BlockCursor, Indent, PropertyKey, SymbolName, ArgumentCursor,
    ObjectCursor, ObjectRestCursor, EndProperty, BeginFunction, EndFunction, ParameterCursor, EndParameter, SnapshotCursor, DerivedClassBody, ArrayArgumentCursor, LoopHeader, LoopHeaderCursor, ElideStatement, LoopBody, LoopTransitionEnd, LoopInitializerCursor, LoopDeclarationCursor, LoopKeyword, BlockBoundary>;
struct OwnedAction { std::size_t owner; std::size_t depth; PrintAction action; };
struct LocatedAnchor { BlockBoundary boundary; AnchorId id; };
struct OwnerTree { FunctionOwnerId owner; std::size_t member; };

class TreePrinter {
 public:
  TreePrinter(const FunctionTree& tree, std::ostream& output,
              EmitBudget limit, analysis::SemanticBudget scratch, const GlobalScope* scope,const OriginalFlow* original=nullptr)
      : tree_(&tree), original_(original), output_(output), limit_(limit.bytes), initial_(scratch),
        remaining_(scratch), scope_(scope) {}
  TreePrinter(const VerifiedFunction& function, std::ostream& output,
              EmitBudget limit, analysis::SemanticBudget scratch, const GlobalScope* scope)
      : function_(&function), tree_(&function.Tree()), output_(output),
        limit_(limit.bytes), initial_(scratch), remaining_(scratch), scope_(scope) {}
  TreePrinter(const ConstructedFamily& family, std::ostream& output,
              EmitBudget limit, analysis::SemanticBudget scratch, const ClassTreeContext* member,
              const GlobalScope* scope, const ClassNames* prepared = nullptr)
      : family_(&family), class_root_(member), output_(output), limit_(limit.bytes), initial_(scratch), remaining_(scratch), scope_(scope), enclosing_names_(prepared) {}
  PrintedFunction Print() {
    TreePrintCensusScope::Visit(Tree());
    if (!function_->ClassCreation() || Tree().callable != CallableKind::kMethod ||
        Tree().strictness != Strictness::kStrict || Tree().self || Tree().capture_scope || !Tree().captures.empty())
      throw Error::kUnsupportedContext;
    // Standalone printing has no verified family/child lookup. Reject before
    // writing the header, never leave a partial fake function body behind.
    for (const auto& expression : Tree().expressions) {
      Step();
      if (std::holds_alternative<FunctionExpr>(expression.expression)) throw Error::kUnsupportedContext;
      if (std::holds_alternative<RealmObjectExpr>(expression.expression)) RecordRealm();
      if (std::holds_alternative<SymbolConstructorExpr>(expression.expression)) RecordSymbol();
      if (const auto* snapshot=std::get_if<ArgumentSnapshotExpr>(&expression.expression)) RecordArgumentSnapshot(snapshot->form);
    }
    Result().identity = Tree().identity;
    Result().method = Tree().method;
    Result().value_revision = Tree().value_revision;
    Result().cfg_revision = Tree().cfg_revision;
    if (!Tree().modules.empty())
      Result().module_binding = PrintedFunction::ModuleBinding{
          *function_->Receipt().record, scope_->module_enclosure->Owner()};
    Site(TreePrintSite::kPositionTable);
    Array(Result().parameters, Tree().parameters.size());
    Array(Result().expressions, Tree().expressions.size());
    Array(Result().properties, Tree().properties.size());
    Array(Result().statements, Tree().statements.size());
    Array(Result().expression_extents, Tree().expressions.size());
    Array(Result().property_extents, Tree().properties.size());
    Array(Result().statement_extents, Tree().statements.size());
    Array(Result().anchors, Tree().anchors.size());
    PrepareAnchors();
    switch (function_->ClassCreation()->placement) {
      case abc::ClassPlacement::kStatic: Append("static "); break;
      case abc::ClassPlacement::kPrototype: break;
      default: throw Error::kUnsupportedContext;
    }
    Key(function_->Name());
    Append("(");
    for (std::size_t i = 0; i < Tree().parameters.size(); ++i) {
      Step();
      const auto& parameter = Tree().parameters[i];
      if (i != 0) Append(", ");
      Result().parameters[i].begin = written_;
      if (parameter.rest) Append("...");
      Name(parameter.symbol);
      if (parameter.initializer) {
        if (parameter.initializer->value >= Tree().expressions.size() ||
            (!Tree().parameter_expressions &&
             !std::holds_alternative<TreeLiteral>(
                 Tree().expressions[parameter.initializer->value].expression) &&
             !std::holds_alternative<SymbolExpr>(
                 Tree().expressions[parameter.initializer->value].expression)))
          throw Error::kUnsupportedContext;
        Append(" = ");
        Push(ExpressionOperand{*parameter.initializer,{ExpressionGrammar::kAssignmentValue}});
        Drain();
      }
      Result().parameters[i].end = written_;
    }
    Append(") ");
    Push(Statement{Tree().body, 0});
    Drain();
    FinalizeNullishRanges();
    for (std::size_t i = 0; i < Tree().anchors.size(); ++i) {
      Step();
      if (std::holds_alternative<FunctionExit>(Tree().anchors[i].position))
        Result().anchors[i] = written_;
    }
    Append("\n");
    result_.bytes = written_;
    Result().allocated_bytes = peak_bytes_;
    Result().work = initial_.work - remaining_.work;
    return std::move(result_);
  }
  PrintedFunction Direct(bool module = false, bool fields = false, std::size_t indent = 0,
      std::optional<ModuleDefaultExport> default_export = {}) {
    default_export_ = default_export;
    module_body_ = module || fields;
    static_fields_ = fields;
    if (Tree().class_scope && !fields) throw Error::kUnsupportedContext;
    TreePrintCensusScope::Visit(Tree());
    Site(TreePrintSite::kStructureVerification);
    CheckStructure();
    if (Tree().callable != CallableKind::kFunction && !IsArrowCallable(Tree().callable) &&
        Tree().callable != CallableKind::kAsyncFunction && Tree().callable != CallableKind::kGenerator &&
        Tree().callable != CallableKind::kAsyncGenerator && !(fields && Tree().callable == CallableKind::kMethod))
      throw Error::kUnsupportedContext;
    if (fields) {
      const auto* body = std::get_if<BlockStmt>(&Tree().statements[Tree().body.value].statement);
      if (!body || !body->child_count || !Tree().parameters.empty() ||
          (!Tree().captures.empty() && !Tree().class_scope) ||
          Tree().inferred_name || Tree().self ||
          !Tree().recovery_issues.empty() ||
          Tree().callable != CallableKind::kMethod ||
          Tree().strictness != Strictness::kStrict)
        throw Error::kUnsupportedContext;
      for (const auto& symbol : Tree().symbols) {
        Step();
        if (symbol.kind != BindingKind::kModule &&
            !(Tree().class_scope && (symbol.kind == BindingKind::kClassInner ||
                                     symbol.kind == BindingKind::kCapture)))
          throw Error::kUnsupportedContext;
      }
      for (std::uint32_t i = 0; i < body->child_count; ++i) {
        Step(); const auto id = Tree().children[body->first_child + i];
        if (DiscardedInvocationRead(Tree(), id)) continue;
        const auto& node = Tree().statements[id.value].statement;
        if (i + 1 == body->child_count) {
          const auto* returned = std::get_if<ReturnStmt>(&node);
          if (!returned) throw Error::kUnsupportedContext;
          if (returned->value) {
            const auto* literal = std::get_if<TreeLiteral>(&Tree().expressions[returned->value->value].expression);
            if (!literal || !std::holds_alternative<UndefinedLiteral>(*literal)) throw Error::kUnsupportedContext;
          }
        } else {
          const auto* statement = std::get_if<ExpressionStmt>(&node);
          if (!statement ||
              !std::holds_alternative<FieldDefinitionExpr>(
                  Tree().expressions[statement->expression.value].expression)) {
            if (!Tree().class_scope) throw Error::kUnsupportedContext;
            static_block_ = true;
          }
        }
      }
      if (static_block_) {
        const auto tail = ImplicitTailCompletion(Tree());
        for (std::size_t s = 0; s < Tree().statements.size(); ++s) {
          Step();
          if (std::holds_alternative<ReturnStmt>(
                  Tree().statements[s].statement) &&
              (!tail || s != tail->statement.value))
            throw Error::kUnsupportedContext;
        }
        for (const auto& expression : Tree().expressions) {
          Step();
          if (std::holds_alternative<FieldDefinitionExpr>(
                  expression.expression))
            throw Error::kUnsupportedContext;
        }
      }
    }
    if (module) { CheckModuleBody(); CheckDefaultExport(); }
    for (const auto& node : Tree().expressions) {
      Step();
      if (std::holds_alternative<FunctionExpr>(node.expression)) throw Error::kUnsupportedContext;
      if (std::holds_alternative<RealmObjectExpr>(node.expression)) RecordRealm();
      if (std::holds_alternative<SymbolConstructorExpr>(node.expression)) RecordSymbol();
      if (const auto* snapshot=std::get_if<ArgumentSnapshotExpr>(&node.expression)) {
        if (fields) throw Error::kUnsupportedContext;
        RecordArgumentSnapshot(snapshot->form);
      }
    }
    Result().identity = Tree().identity; Result().method = Tree().method;
    Result().value_revision = Tree().value_revision; Result().cfg_revision = Tree().cfg_revision;
    Result().fragment = fields ? FunctionFragment::kStaticFields :
        module ? FunctionFragment::kModuleBody : FunctionFragment::kConditionalExpression;
    Result().incomplete = !Tree().recovery_issues.empty();
    Site(TreePrintSite::kPositionTable);
    Array(Result().parameters, Tree().parameters.size()); Array(Result().expressions, Tree().expressions.size());
    Array(Result().properties, Tree().properties.size()); Array(Result().statements, Tree().statements.size());
    Array(Result().expression_extents, Tree().expressions.size());
    Array(Result().property_extents, Tree().properties.size());
    Array(Result().statement_extents, Tree().statements.size());
    Array(Result().anchors, Tree().anchors.size()); PrepareAnchors();
    PrepareClassNames();
    if (static_block_) {
      Run(Indent{indent});
      Append("static {\n");
    }
    if (module || fields)
      Push(Statement{Tree().body, indent + (static_block_ ? 1 : 0)});
    else
      Push(BeginFunction{});
    Drain();
    FinalizeNullishRanges();
    if (static_block_) {
      Run(Indent{indent});
      Append("}");
    }
    Append("\n");
    Result().bytes = written_;
    Result().allocated_bytes = peak_bytes_;
    Result().work = initial_.work - remaining_.work;
    return std::move(result_);
  }
  PrintedFamilyDraft Draft(std::size_t indent = 0, bool module = false,
      std::optional<ModuleDefaultExport> default_export = {}) {
    default_export_ = default_export;
    module_body_ = module;
    if (family_->root.value >= family_->members.size()) throw Error::kUnsupportedContext;
    if (class_root_) {
      const auto& root = family_->members[family_->root.value].tree;
      if ((!IsMethodCallable(root.callable) && root.callable != CallableKind::kBaseConstructor && root.callable != CallableKind::kDerivedConstructor) ||
          root.callable != class_root_->Callable() || root.inferred_name ||
          root.identity != class_root_->Identity() || root.method != class_root_->Target() ||
          (!class_root_->Constructor() && class_root_->Placement() != abc::ClassPlacement::kStatic &&
           class_root_->Placement() != abc::ClassPlacement::kPrototype)) throw Error::kUnsupportedContext;
    }
    TreePrintSiteScope positions_site(TreePrintSite::kPositionTable);
    Array(draft_.members, family_->members.size()); Array(draft_.functions, family_->members.size());
    Array(locations_, family_->members.size()); Array(owners_, family_->members.size());
    Array(printed_, family_->members.size());
    Array(native_names_, family_->members.size());
    for (std::size_t i = 0; i < family_->members.size(); ++i) {
      current_ = i; Step();
      TreePrintCensusScope::Visit(Tree());
      Site(TreePrintSite::kStructureVerification);
      CheckStructure();
      if (Tree().strictness != Strictness::kStrict ||
          (Tree().callable != CallableKind::kFunction && !IsArrowCallable(Tree().callable) &&
           !IsMethodCallable(Tree().callable) && Tree().callable != CallableKind::kBaseConstructor && Tree().callable != CallableKind::kDerivedConstructor && Tree().callable != CallableKind::kAsyncFunction &&
           Tree().callable != CallableKind::kGenerator && Tree().callable != CallableKind::kAsyncGenerator))
        throw Error::kUnsupportedContext;
      // Only the actual class root is a class member. An arrow method carrier
      // retains its original callable kind and cannot observe its home object.
      if ((IsMethodCallable(Tree().callable) || Tree().callable == CallableKind::kBaseConstructor || Tree().callable == CallableKind::kDerivedConstructor) && !ClassRoot())
        throw Error::kUnsupportedContext;
      owners_[i] = {Tree().owner, i};
      Result().identity = Tree().identity; Result().method = Tree().method;
      Result().value_revision = Tree().value_revision; Result().cfg_revision = Tree().cfg_revision;
      if (ModuleBody()) { CheckModuleBody(); CheckDefaultExport(); }
      Result().fragment = Tree().ImplicitConstructor() ? FunctionFragment::kImplicitConstructor : ModuleBody() ? FunctionFragment::kModuleBody :
          ClassRoot() ? FunctionFragment::kClassMember : FunctionFragment::kConditionalExpression;
      Result().incomplete = !Tree().recovery_issues.empty();
      if (!Tree().modules.empty() && scope_ && scope_->module_enclosure) {
        if (!family_->members[i].record) throw Error::kUnsupportedContext;
        has_external_binding_ = true;
        Result().module_binding = PrintedFunction::ModuleBinding{
            *family_->members[i].record, scope_->module_enclosure->Owner()};
      }
      Site(TreePrintSite::kPositionTable);
      Array(Result().parameters, Tree().parameters.size()); Array(Result().expressions, Tree().expressions.size());
      Array(Result().properties, Tree().properties.size()); Array(Result().statements, Tree().statements.size());
      Array(Result().expression_extents, Tree().expressions.size());
      Array(Result().property_extents, Tree().properties.size());
      Array(Result().statement_extents, Tree().statements.size());
      Array(Result().anchors, Tree().anchors.size()); PrepareAnchors();
      for (const auto& node : Tree().expressions) {
        Step();
        if (std::holds_alternative<RealmObjectExpr>(node.expression)) RecordRealm();
        if (std::holds_alternative<SymbolConstructorExpr>(node.expression)) RecordSymbol();
        if (const auto* snapshot=std::get_if<ArgumentSnapshotExpr>(&node.expression)) RecordArgumentSnapshot(snapshot->form);
      }
    }
    if (has_external_binding_) {
      Site(TreePrintSite::kDestinationVerification);
      const auto check = VerifyFamilyDestination(*family_, scope_, remaining_,
          module_body_ ? ModuleRole::kInitializer : ModuleRole::kReader);
      const auto* enclosure = std::get_if<FamilyEnclosure>(&check);
      if (!enclosure) {
        switch (std::get<FamilyEnclosureFailure>(check).kind) {
          case FamilyEnclosureError::kResourceLimit: throw Error::kResourceLimit;
          case FamilyEnclosureError::kWorkLimit: throw Error::kWorkLimit;
          default: throw Error::kUnsupportedContext;
        }
      }
      Observe(enclosure->allocated_bytes); Step(enclosure->work);
    }
    std::sort(owners_.begin(), owners_.end(), [&](const auto& a, const auto& b) { Step(); return a.owner < b.owner; });
    for (std::size_t i = 1; i < owners_.size(); ++i) {
      Step(); if (owners_[i - 1].owner == owners_[i].owner) throw Error::kUnsupportedContext;
    }
    PrepareFamilyStatus();
    PrepareNativeNames();
    PrepareClassNames();
    current_ = family_->root.value; depth_ = indent;
    Push(BeginFunction{}); Drain();
    for (std::size_t i = 0; i < family_->members.size(); ++i) { current_ = i; FinalizeNullishRanges(); }
    Append("\n");
    for (const auto emitted : printed_) { Step(); if (!emitted) throw Error::kUnsupportedContext; }
    draft_.bytes = written_; draft_.allocated_bytes = peak_bytes_;
    draft_.work = initial_.work - remaining_.work;
    return std::move(draft_);
  }
 private:
  void ConcurrentRefusal(const ConcurrentFailure& detail) {
    auto failure=TreePrintFailure{TreePrintError::kUnsupportedContext};failure.concurrent=detail;
    failure.location.method=detail.method;failure.written_bytes=written_;throw failure;
  }
  struct ConcurrentFrame {bool owns=false;std::optional<ConcurrentReferenceProof> proof;};
  std::vector<ConcurrentFrame> concurrent_frames_;
  std::vector<std::vector<std::optional<ModuleSymbol>>> concurrent_modules_;
  std::vector<std::optional<ModuleSymbol>> direct_concurrent_modules_;
  std::u16string_view ContextName(NameContext context) const {
    if (const auto* name = std::get_if<NameId>(&context)) return Tree().names[name->value];
    if (std::holds_alternative<DefaultInferredName>(context)) return u"default";
    if (const auto* property = std::get_if<PropertyInferredName>(&context)) return Tree().names[property->key.value];
    return u"";
  }
  void PrepareClassNames() {
    if(enclosing_names_) {
      if(enclosing_names_->scope!=scope_)throw Error::kUnsupportedContext;
      return;
    }
    const auto prepared = family_ ? std::span<const SourceVariables>(family_variables_) :
        std::span<const SourceVariables>(&variables_, 1);
    Site(TreePrintSite::kClassNamePlanning);
    auto planned = family_ ? PlanPrintedClassNames(*family_, scope_, remaining_, prepared) :
        PlanPrintedClassNames(Tree(), scope_, remaining_, prepared);
    if (const auto* error = std::get_if<TreeStructureError>(&planned)) {
      if (*error == TreeStructureError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == TreeStructureError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kUnsupportedContext;
    }
    class_names_ = std::get<ClassNames>(std::move(planned));
    Admit(class_names_.allocated_bytes, 1); Step(class_names_.work);
  }
  const ClassNames& Names() const {return enclosing_names_?*enclosing_names_:class_names_;}
  bool NativeName() const { return family_ && native_names_[current_]; }
  void PrepareNativeNames() {
    const auto set = [&](ExprId id, NameContext context) {
      const auto* function = std::get_if<FunctionExpr>(&Tree().expressions[id.value].expression);
      if (!function) return;
      const auto child = std::lower_bound(owners_.begin(), owners_.end(), function->child,
          [&](const auto& item, FunctionOwnerId owner) { Step(); return item.owner < owner; });
      if (child == owners_.end() || child->owner != function->child) throw Error::kUnsupportedContext;
      const auto& target = family_->members[child->member].tree;
      const auto expected = ContextName(context);
      if (target.inferred_name)
        Step(std::min(target.names[target.inferred_name->value].size(), expected.size()) + 1);
      const bool property = std::holds_alternative<PropertyInferredName>(context);
      if (property && (!target.inferred_name || target.self ||
          !target.names[target.inferred_name->value].empty() || target.invocation != InvocationForm::kDeclared))
        throw Error::kUnsupportedContext;
      native_names_[child->member] = target.inferred_name && !target.self &&
          target.invocation == InvocationForm::kDeclared &&
          (property || target.names[target.inferred_name->value] == expected);
    };
    // ECMA IsAnonymousFunctionDefinition/NamedEvaluation belongs to the actual
    // expression destination. Identifier bindings and own data properties infer
    // names; calls, returns, arrays, sequence/conditional/logical operands and
    // member assignments do not. Parentheses preserve this grammar context.
    // No inner self binding is introduced. Retain creation-self envelopes and
    // arrow-method argument carriers. Standalone fragments have no destination
    // proof and retain their naming wrapper. Regression: native_function_names.
    // Walk retained destinations once. A forwarded value suppresses name
    // inference exactly as PrintOriginalExpression's GetValue envelope does.
    // Omitted stores must never override the name at the surviving occurrence.
    for (std::size_t i = 0; i < family_->members.size(); ++i) {
      current_ = i;
      struct Destination { ExprId expression; NameContext name; };
      std::vector<Destination> pending; Array(pending, Tree().expressions.size());
      std::size_t count = 0;
      const auto push = [&](ExprId expression, NameContext name = {}) {
        Step(); if (count >= pending.size()) throw Error::kUnsupportedContext;
        pending[count++] = {expression, name};
      };
      for (const auto& parameter : Tree().parameters) {
        Step(); if (parameter.initializer) push(*parameter.initializer, Tree().symbols[parameter.symbol.value].name);
      }
      for (std::size_t at = 0; at < Tree().statements.size(); ++at) {
        Step(); if (Initializers().omitted[at]) continue;
        const auto& statement = Tree().statements[at].statement;
        if(const auto& completion=Initializers().declaration_completions[at]) {
          if(completion->evaluation)push(*completion->evaluation);
          continue;
        }
        if(const auto& value=Initializers().declaration_values[at]) {
          push(value->value,value->target?NameContext{Tree().symbols[Initializers().Storage(*value->target).value].name}:NameContext{});
          continue;
        }
        if (const auto* declaration = std::get_if<DeclarationStmt>(&statement)) {
          const bool default_value = ModuleBody() && default_export_ &&
              default_export_->declaration == StmtId{static_cast<std::uint32_t>(at)};
          if (declaration->initializer) push(*declaration->initializer, default_value ? NameContext{DefaultInferredName{}} :
              NameContext{Tree().symbols[declaration->symbol.value].name});
        } else VisitStatementExpressions(statement, [&](ExprId expression) {push(expression);});
      }
      while (count) {
        Step(); const auto destination = pending[--count];
        const auto id = destination.expression;
        if(Initializers().transports[id.value])continue;
        if (const auto replacement = Initializers().replacements[id.value]) {push(*replacement); continue;}
        const auto& expression = Tree().expressions[id.value].expression;
        set(id, destination.name);
        if (const auto* object = std::get_if<ObjectExpr>(&expression)) {
          for (std::uint32_t p = 0; p < object->properties.count; ++p) {
            const auto& property = Tree().properties[object->properties.first + p];
            push(property.value, object->kind == AggregateKind::kObject && property.key ? (property.name_setting == analysis::PropertyNameSetting::kPropertyKey ?
                NameContext{PropertyInferredName{*property.key}} : NameContext{*property.key}) : NameContext{});
          }
        } else VisitExpressionChildren(Tree(), expression, [&](ExprId child) {
          NameContext name;
          if (const auto* assignment = std::get_if<AssignmentExpr>(&expression); assignment && child == assignment->value) {
            if (const auto* target = std::get_if<SymbolTarget>(&assignment->target)) name = Tree().symbols[Initializers().Storage(target->symbol).value].name;
          } else if (const auto* field = std::get_if<FieldDefinitionExpr>(&expression); field && child == field->value) name = field->name;
          push(child, name);
        });
      }
    }
    // Extra allocation is conditional on the final syntax, not merely metadata
    // carrying a runtime name. Fail preflight while the output is still empty.
    for (std::size_t i = 0; i < family_->members.size(); ++i) {
      current_ = i; Step();
      if (Tree().inferred_name && !NativeName() && (!Tree().self || Tree().self_form == SelfForm::kCreationBinding) &&
          ResourceFailures(Tree().identity.contract) != ResourceFailureScope::kExcluded)
        throw Error::kUnsupportedContext;
    }
  }
  bool ModuleBody() const { return module_body_ && (!family_ || current_ == family_->root.value); }
  void CheckDefaultExport() {
    if (!default_export_) return;
    const auto checked = CheckModuleDefaultExport(Tree(), family_, *default_export_, remaining_);
    if (const auto* error = std::get_if<TreePrintFailure>(&checked)) throw *error;
    const auto& check = std::get<DefaultExportCheck>(checked);
    if (!check.eligible) throw Error::kUnsupportedContext;
    Admit(check.allocated_bytes, 1); Step(check.work);
  }
  void CheckModuleBody() {
    const auto* body = std::get_if<BlockStmt>(&Tree().statements[Tree().body.value].statement);
    if (!body || !body->child_count || !Tree().parameters.empty() || !Tree().captures.empty() ||
        Tree().self || Tree().callable != CallableKind::kFunction || Tree().strictness != Strictness::kStrict)
      throw Error::kUnsupportedContext;
    const auto last = Tree().children[body->first_child + body->child_count - 1];
    if (!std::holds_alternative<ReturnStmt>(Tree().statements[last.value].statement)) throw Error::kUnsupportedContext;
    for (std::size_t i = 0; i < Tree().statements.size(); ++i) {
      Step();
      if (std::holds_alternative<ReturnStmt>(Tree().statements[i].statement) && i != last.value)
        throw Error::kUnsupportedContext;
    }
    for (const auto& node : Tree().expressions) {
      Step();
      if (std::holds_alternative<ThisExpr>(node.expression) ||
          std::holds_alternative<AwaitExpr>(node.expression) ||
          std::holds_alternative<YieldExpr>(node.expression) ||
          std::holds_alternative<ArgumentSnapshotExpr>(node.expression)) throw Error::kUnsupportedContext;
    }
  }
  void PrepareFamilyStatus() {
    // Follow the actual syntax ownership, not a possibly stale construction
    // order. A gap affects every enclosing function, never its siblings.
    // Leaf-to-root reduction is iterative and visits each creation once.
    std::vector<std::optional<std::size_t>> parents;
    std::vector<std::size_t> children, pending;
    Array(parents, family_->members.size());
    Array(children, family_->members.size());
    Array(pending, family_->members.size());
    for (std::size_t parent = 0; parent < family_->members.size(); ++parent) {
      for (const auto& expression : family_->members[parent].tree.expressions) {
        Step();
        const auto* function = std::get_if<FunctionExpr>(&expression.expression);
        if (!function) continue;
        const auto found = std::lower_bound(owners_.begin(), owners_.end(), function->child,
            [&](const auto& item, FunctionOwnerId owner) { Step(); return item.owner < owner; });
        if (found == owners_.end() || found->owner != function->child ||
            found->member == family_->root.value || parents[found->member])
          throw Error::kUnsupportedContext;
        parents[found->member] = parent;
        ++children[parent];
      }
    }
    std::size_t count = 0;
    for (std::size_t i = 0; i < parents.size(); ++i) {
      Step();
      if (i != family_->root.value && !parents[i]) throw Error::kUnsupportedContext;
      if (children[i] == 0) pending[count++] = i;
    }
    for (std::size_t next = 0; next < count; ++next) {
      Step();
      const auto child = pending[next];
      if (!parents[child]) continue;
      const auto parent = *parents[child];
      draft_.members[parent].incomplete |= draft_.members[child].incomplete;
      if (--children[parent] == 0) pending[count++] = parent;
    }
    if (count != parents.size()) throw Error::kUnsupportedContext;
  }
  void RecordRealm() {
    has_external_binding_ = true;
    if (Result().realm_binding) return;
    if (scope_ && scope_->realm_binding) Step(scope_->realm_binding->name.size());
    const auto binding = ResolveRealmBinding(Tree().identity.contract, scope_);
    if (!binding) throw Error::kUnsupportedContext;
    Step(binding->name.size()); Admit(binding->name.size(), sizeof(char16_t));
    Result().realm_binding = PrintedFunction::RealmBinding{binding->policy, std::u16string{binding->name}};
    Admit(Result().realm_binding->name.capacity() - binding->name.size(), sizeof(char16_t));
  }
  void RecordSymbol() {
    has_external_binding_ = true;
    if (Result().symbol_binding) return;
    if (scope_ && scope_->symbol_binding) Step(scope_->symbol_binding->name.size());
    const auto binding = ResolveSymbolBinding(scope_);
    if (!binding) throw Error::kUnsupportedContext;
    Step(binding->size()); Admit(binding->size(), sizeof(char16_t));
    Result().symbol_binding = PrintedFunction::SymbolBinding{std::u16string{*binding}};
    Admit(Result().symbol_binding->name.capacity() - binding->size(), sizeof(char16_t));
  }
  bool ClassRoot() const { return class_root_ && current_ == family_->root.value; }
  void RecordArgumentSnapshot(ArgumentSnapshotForm form) {
    if(form==ArgumentSnapshotForm::kNativeEntry)return;
    has_external_binding_ = true;
    if (Result().argument_snapshot_binding) return;
    if (scope_ && scope_->argument_snapshot_binding) Step(scope_->argument_snapshot_binding->name.size());
    const auto binding = ResolveArgumentSnapshotBinding(scope_);
    if (!binding) throw Error::kUnsupportedContext;
    Step(binding->size()); Admit(binding->size(), sizeof(char16_t));
    Result().argument_snapshot_binding = PrintedFunction::ArgumentSnapshotBinding{
        std::u16string{*binding}, scope_->argument_snapshot_binding->abi};
    Admit(Result().argument_snapshot_binding->name.capacity() - binding->size(), sizeof(char16_t));
  }
  const OriginalFlow* Original() const {
    if(family_)return family_->members[current_].flow.original.get();
    return function_?function_->Flow().original.get():original_;
  }
  const FunctionTree& Tree() const { return family_ ? family_->members[current_].tree : *tree_; }
  PrintedFunction& Result() { return family_ ? draft_.members[current_] : result_; }
  std::vector<LocatedAnchor>& Located() { return family_ ? locations_[current_] : located_; }
  void Drain() {
    while (!actions_.empty()) {
      Step();
      const auto action = actions_.back();
      actions_.pop_back();
      current_ = action.owner;
      depth_ = action.depth;
      Site(TreePrintSite::kTokenOutput);
      std::visit([&](const auto& value) {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,ExprId>) Locate(value);
        else if constexpr(std::is_same_v<T,Statement>) Locate(value.id);
        Run(value);
      }, action.action);
    }
  }
  void Site(TreePrintSite site) {
    if(auto* trace=TreePrintTrace::Active()) {
      trace->failure.site=site; trace->failure.location={};trace->failure.anchor.reset();
      trace->failure.location.method=Tree().method;trace->failure.owner=Tree().owner;
      trace->failure.written_bytes=written_;
    }
  }
  void Origin(TreeOriginId origin) {
    if(auto* trace=TreePrintTrace::Active();trace && origin.value<Tree().origins.size())
      std::visit([&](const auto& value) {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,OriginalSite>||std::is_same_v<T,ElidedOrigin>) {
          trace->failure.location.working_pc=value.pc;
          trace->failure.location.pc=value.original.value_or(value.pc);
        }
      },Tree().origins[origin.value]);
  }
  void Locate(ExprId id) {if(auto* trace=TreePrintTrace::Active())trace->failure.location.expression=id; if(id.value<Tree().expressions.size())Origin(Tree().expressions[id.value].origin);}
  void Locate(StmtId id) {if(auto* trace=TreePrintTrace::Active())trace->failure.location.statement=id; if(id.value<Tree().statements.size())Origin(Tree().statements[id.value].origin);}
  // Preserve the measured peak, but return scratch only after the result's
  // expression_scopes and the verifier's internal owner have both died.
  void CheckStructure() {
    std::size_t peak=0,input=0;std::uint64_t work=0;
    {
      auto checked=VerifyTreeStructure(Tree(),remaining_,scope_);
      if(const auto* error=std::get_if<TreeStructureError>(&checked)) {
        if(*error==TreeStructureError::kResourceLimit)throw Error::kResourceLimit;
        if(*error==TreeStructureError::kWorkLimit)throw Error::kWorkLimit;
        throw Error::kUnsupportedContext;
      }
      const auto& structure=std::get<TreeStructure>(checked);
      peak=structure.allocated_bytes;input=structure.input_bytes;work=structure.work;
    }
    Observe(peak);Admit(input,1);Step(work);
  }
  void Observe(std::size_t bytes) {
    Admit(bytes,1);remaining_.bytes+=bytes;
  }
  void Step(std::uint64_t count = 1) {
    if (count > remaining_.work) { PrintCharge(TreePrintBudgetKind::kWork,count,remaining_.work,initial_.work); throw Error::kWorkLimit; }
    remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    if (count > remaining_.bytes / width) { PrintByteCharge(count,width,remaining_.bytes,initial_.bytes,peak_bytes_); throw Error::kResourceLimit; }
    remaining_.bytes -= count * width;
    peak_bytes_ = std::max(peak_bytes_, initial_.bytes - remaining_.bytes);
  }
  template <class T> void Array(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T)); Step(count);
    values.resize(count);
    Admit(values.capacity() - count, sizeof(T));
  }
  void Push(PrintAction action) {
    TreePrintSiteScope site(TreePrintSite::kActionStack);
    if (actions_.size() == actions_.capacity()) {
      // Geometric growth; retain replaced allocations in the charge, covering
      // the reserve peak without a guessed host-stack depth bound.
      constexpr std::size_t kInitialActions = 16;
      const auto old = actions_.capacity();
      if (old > std::numeric_limits<std::size_t>::max() / 2) {
        PrintCharge(TreePrintBudgetKind::kCapacityOverflow,{},remaining_.bytes,initial_.bytes,peak_bytes_);throw Error::kResourceLimit; }
      const auto capacity = old == 0 ? kInitialActions : old * 2;
      Admit(capacity, sizeof(OwnedAction));
      actions_.reserve(capacity);
      Admit(actions_.capacity() - capacity, sizeof(OwnedAction));
    }
    actions_.push_back({current_, depth_, action});
  }
  void Append(std::string_view text) {
    TreePrintSiteScope site(TreePrintSite::kTokenOutput);
    Step(text.size());
    if (text.size() > limit_ - written_) { PrintCharge(TreePrintBudgetKind::kOutputBytes,text.size(),limit_-written_,limit_,written_); throw Error::kOutputLimit; }
    output_.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output_) throw Error::kWrite;
    written_ += text.size();
    if(auto* trace=TreePrintTrace::Active())trace->failure.written_bytes=written_;
  }
  void Quoted(std::u16string_view text) {
    TreePrintSiteScope site(TreePrintSite::kTokenOutput);
    Step(text.size());
    const auto written = WriteStringLiteral(text, output_, {limit_ - written_});
    if (const auto* bytes = std::get_if<std::size_t>(&written)) {written_ += *bytes;if(auto* trace=TreePrintTrace::Active())trace->failure.written_bytes=written_;}
    else {
      switch (std::get<EmitError>(written)) {
        case EmitError::kOutputLimit: throw Error::kOutputLimit;
        case EmitError::kWrite: throw Error::kWrite;
        case EmitError::kInvalidBinding:
        case EmitError::kInvalidExpression:
        case EmitError::kNaNPayload: throw Error::kInvalidLiteral;
      }
    }
  }
  void Identifier(std::u16string_view name) {
    // Scope/name validation has already restricted binding identifiers to ASCII.
    // Property keys outside that subset are emitted as exact UTF-16 strings.
    for (const auto unit : name) {
      const char byte = static_cast<char>(unit);
      Append(std::string_view(&byte, 1));
    }
  }
  void Key(std::u16string_view name) {
    Step(name.size());
    if (IsSourceIdentifier(name)) Identifier(name);
    else Quoted(name);
  }
  void Name(SymbolId id) {
    const auto stored=Initializers().Storage(id);
    if(!concurrent_frames_.empty()&&concurrent_frames_.back().proof) {
      const auto& modules=family_?concurrent_modules_[current_]:direct_concurrent_modules_;
      if(stored.value<modules.size()&&modules[stored.value])
        if(const auto failure=concurrent_frames_.back().proof->Observe(*modules[stored.value]))ConcurrentRefusal(*failure);
    }
    Identifier(Tree().names[Tree().symbols[stored.value].name.value]);
  }
  template <class T> void Numeric(T value) {
    // General to_chars uses a shortest round-tripping representation. The fixed
    // scratch covers sign, max_digits10, exponent marker/sign and decimal exponent.
    constexpr std::size_t kNumericBuffer = std::numeric_limits<double>::max_digits10 +
        std::numeric_limits<int>::digits10 + 8;
    std::array<char, kNumericBuffer> buffer{};
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (converted.ec != std::errc{}) throw Error::kInvalidLiteral;
    Append(std::string_view(buffer.data(), converted.ptr - buffer.data()));
  }
  void Literal(UndefinedLiteral) { Append("(void 0)"); }
  void Literal(NullLiteral) { Append("null"); }
  void Literal(BooleanLiteral value) { Append(value.value ? "true" : "false"); }
  void Literal(IntegerLiteral value) {
    if (value.value < 0) Append("(");
    Numeric(value.value);
    if (value.value < 0) Append(")");
  }
  void Literal(NumberLiteral value) {
    const double number = std::bit_cast<double>(value.bits);
    if (std::isnan(number)) {
      // The explicit numeric contract excludes payload observation. Keep the
      // original AST bits for origin checks; only this textual boundary uses
      // native NaN. Private-tag encodings never acquire a Number premise.
      if (value.bits != std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN()) &&
          !(Arithmetic(Tree().identity.contract) == analysis::ArithmeticSemantics::kSourceNumeric &&
            analysis::detail::FloatNumberKinds({value.bits}).IsNumber()))
        throw Error::kInvalidLiteral;
      Append("(0 / 0)");
    } else if (std::isinf(number)) {
      Append(std::signbit(number) ? "(-1 / 0)" : "(1 / 0)");
    } else {
      if (std::signbit(number)) Append("(");
      Numeric(number);
      if (std::signbit(number)) Append(")");
    }
  }
  void Literal(TextId id) { Quoted(Tree().texts[id.value]); }
  void ContextLiteral(const TreeLiteral& node,const TreeOperatorContext& context) {
    // The spelling `void 0` is a UnaryExpression, not a primary literal.
    // These complete grammar positions accept it directly. All operator,
    // postfix and unknown uses retain the existing envelope.
    if(!ReadableLiteralEnvelope(node,context).Parenthesized()) {
      Append("void 0");return;
    }
    Expression(node);
  }
  void Expression(NewTargetExpr) {
    if (ModuleBody()) throw Error::kUnsupportedContext;
    Append("new.target");
  }
  void Expression(TemplateObjectExpr) { throw Error::kUnsupportedContext; }
  void Expression(RegExpExpr node) {
    auto checked=BuildRegExpLiteral(Tree().texts[node.text.value],node.flags,remaining_);
    if(const auto* error=std::get_if<RegExpLiteralError>(&checked))throw *error==RegExpLiteralError::kResourceLimit?Error::kResourceLimit:*error==RegExpLiteralError::kWorkLimit?Error::kWorkLimit:Error::kInvalidLiteral;
    const auto& literal=std::get<RegExpLiteralPlan>(checked);Admit(literal.allocated_bytes,1);Step(literal.work);Append(literal.literal);
  }
  void Expression(BigIntExpr node) {
    for (const auto ch : Tree().texts[node.text.value]) { const char c = static_cast<char>(ch); Append({&c, 1}); }
    Append("n");
  }
  void Expression(const TreeLiteral& node) {
    std::visit([&](auto value) { Literal(value); }, node);
  }
  void Expression(SymbolExpr node) { Name(node.symbol); }
  void Expression(GlobalNameExpr node) { Identifier(Tree().names[node.name.value]); }
  void Expression(RealmObjectExpr) {
    if (!Result().realm_binding) throw Error::kUnsupportedContext;
    Identifier(Result().realm_binding->name);
  }
  void Expression(SymbolConstructorExpr) {
    if (!Result().symbol_binding) throw Error::kUnsupportedContext;
    Identifier(Result().symbol_binding->name);
  }
  void Expression(ThisExpr) { Append("this"); }
  void Expression(ArgumentSnapshotExpr node) {
    if(node.form==ArgumentSnapshotForm::kNativeEntry){Append("arguments");return;}
    if (!Result().argument_snapshot_binding) throw Error::kUnsupportedContext;
    Append("(0, "); Identifier(Result().argument_snapshot_binding->name);
    Append(")(arguments, [");
    Push(std::string_view("])"));
    Push(SnapshotCursor{node.slots, 0});
  }
  void PrintMemberKey(const MemberKey& key, bool optional) {
    if (const auto* name = std::get_if<NameId>(&key)) Push(PropertyKey{*name, optional});
    else {
      Push(std::string_view("]"));
      Push(std::get<ComputedKey>(key).expression);
      Push(std::string_view(optional ? "?.[" : "["));
    }
  }
  void Member(ExprId base, const MemberKey& key, PostfixContext context = PostfixContext::kMemberBase, bool optional = false, bool statement_start = false) {
    PrintMemberKey(key, optional); Push(ExpressionOperand{base, {context}, {}, ExpressionObservation::kValue, false, statement_start});
  }
  void SuperMember(const MemberExpr& node) {
    const auto begin = written_; Append("super");
    PrintMemberKey(node.key, false);
    Push(ProjectedExpression{node.base, begin, written_});
  }
  void Expression(const MemberExpr& node) {
    if (node.lookup == analysis::PropertyLookup::kSuper) SuperMember(node);
    else Member(node.base, node.key);
  }
  void Expression(const DeleteExpr& node) {
    if (Tree().strictness != Strictness::kStrict) throw Error::kUnsupportedContext;
    Append("(delete "); Push(std::string_view(")")); Member(node.base, node.key);
  }
  void Expression(AwaitExpr node) {
    Append("await ");
    Push(ExpressionOperand{node.operand, {ExpressionPrecedence::kUnary}});
  }
  void Expression(YieldExpr node) {
    switch (node.form) {
      case YieldForm::kDirect: Append("(yield "); break;
      case YieldForm::kDelegate: Append("(yield* "); break;
    }
    Push(std::string_view(")")); Push(node.operand);
  }
  void Expression(UnaryExpr node) {
    Append(TreeUnaryToken(node.operation));
    // A VM value load throws for an unresolvable global; bare typeof would
    // suppress that exception. Lexical symbols retain TDZ errors, and other
    // operand syntax already obtains a value. Do not wrap those in a comma.
    if (node.operation == TreeUnary::kTypeof &&
        std::holds_alternative<GlobalNameExpr>(Tree().expressions[node.operand.value].expression)) {
      Append("(0, "); Push(std::string_view(")")); Push(node.operand);
    } else Push(ExpressionOperand{node.operand, {node.operation}, {},
        node.operation == TreeUnary::kNot ? ExpressionObservation::kTruthiness : ExpressionObservation::kValue});
  }
  void Expression(BinaryExpr node, bool statement_start = false) {
    Push(ExpressionOperand{node.right, {node.operation, OperandSide::kRight}});
    Push(TreeBinaryToken(node.operation));
    Push(ExpressionOperand{node.left, {node.operation, OperandSide::kLeft}, {}, ExpressionObservation::kValue, false, statement_start});
  }
  void Expression(LogicalExpr node, ExpressionObservation observation, bool statement_start = false) {
    // &&/|| preserve truthiness and short-circuit effects in a Boolean context.
    // ?? selects by nullness: removing a Boolean conversion from its left would
    // change whether the right runs. Exact-value consumers keep both operands.
    auto left = observation == ExpressionObservation::kDiscarded ? ExpressionObservation::kTruthiness : observation;
    auto right = observation;
    if (node.operation == TreeLogical::kNullish) {
      left = ExpressionObservation::kValue;
      if (observation != ExpressionObservation::kDiscarded) right = ExpressionObservation::kValue;
    }
    Push(ExpressionOperand{node.right, {node.operation, OperandSide::kRight}, {}, right});
    Push(TreeLogicalToken(node.operation));
    Push(ExpressionOperand{node.left, {node.operation, OperandSide::kLeft}, {}, left, false, statement_start});
  }
  void Expression(PatternFailureExpr) { Append("({} = null)"); }
  void Expression(ObjectRestExpr) { throw Error::kUnsupportedContext; }
  void Expression(SequenceExpr node, ExpressionObservation observation, bool statement_start = false) {
    Push(ExpressionOperand{node.right, {ExpressionGrammar::kAssignmentValue}, {}, observation});
    Push(std::string_view(", "));
    Push(ExpressionOperand{node.left, {ExpressionGrammar::kExpression}, {}, ExpressionObservation::kDiscarded, false, statement_start});
  }
  void Expression(MemberMutationExpr node) {
    const auto target=[&]{ Member(node.target.base,node.target.key); };
    if(const auto* update=std::get_if<MemberIncrement>(&node.operation)) {
      const std::string_view token=update->direction==UpdateDirection::kIncrement?"++":"--";
      if(update->result==UpdateResult::kPostfix)Push(token);
      target();if(update->result==UpdateResult::kPrefix)Push(token);
    } else {
      const auto& compound=std::get<MemberCompound>(node.operation);
      Push(ExpressionOperand{compound.rhs,{ExpressionGrammar::kAssignmentValue}});
      auto token=TreeBinaryToken(compound.operation);
      Push(std::string_view("= "));Push(token.substr(1,token.size()-2));Push(std::string_view(" "));target();
    }
  }
  void Expression(UpdateExpr node) {
    const auto token = node.direction == UpdateDirection::kIncrement ? "++" : "--";
    if (node.result == UpdateResult::kPostfix) Push(std::string_view(token));
    Push(SymbolName{node.symbol});
    if (node.result == UpdateResult::kPrefix) Append(token);
  }
  void Expression(ConditionalExpr node, ExpressionObservation observation, bool statement_start = false) {
    auto condition = node.condition;
    bool inverted = false;
    // ECMA-262 2024 13.14: only the condition's truthiness is observed. Strip
    // NOT polarity and swap the retained arms, without moving or replaying any
    // operand. This is language AST algebra, never a bytecode shape admission.
    while (const auto* unary = std::get_if<UnaryExpr>(&Tree().expressions[condition.value].expression)) {
      Step(); if (unary->operation != TreeUnary::kNot) break;
      inverted = !inverted; condition = unary->operand;
    }
    Push(ExpressionOperand{inverted ? node.consequent : node.alternate, {ExpressionGrammar::kAssignmentValue}, {}, observation});
    Push(std::string_view(" : "));
    Push(ExpressionOperand{inverted ? node.alternate : node.consequent, {ExpressionGrammar::kAssignmentValue}, {}, observation});
    Push(std::string_view(" ? "));
    // Omitted NOTs retain the actual condition's span. Their end actions run
    // before either branch; no node is falsely located over a swapped arm.
    for (auto id = node.condition; id != condition;) {
      Step(); BeginExpression(id);
      id = std::get<UnaryExpr>(Tree().expressions[id.value].expression).operand;
    }
    Push(ExpressionOperand{condition, {ExpressionGrammar::kConditionalTest}, {}, ExpressionObservation::kTruthiness, false, statement_start});
  }
  void Expression(const FieldDefinitionExpr& node) {
    if (!static_fields_) throw Error::kUnsupportedContext;
    // Native static syntax has an implicit receiver. Retained receiver reads
    // remain representable internally, but need a separate projection proof
    // before this printer can omit their storage/event.
    const auto* receiver = std::get_if<ThisExpr>(&Tree().expressions[node.receiver.value].expression);
    if (!receiver || receiver->owner != Tree().owner) throw Error::kUnsupportedContext;
    const auto& name = Tree().names[node.name.value];
    Append("static ");
    if (ClassifyObjectKey(name) == MemberKeySyntax::kIdentifier && name != u"prototype" && name != u"constructor")
      Identifier(name);
    else { Append("["); Quoted(name); Append("]"); }
    const auto* literal = std::get_if<TreeLiteral>(&Tree().expressions[node.value.value].expression);
    if (literal && std::holds_alternative<UndefinedLiteral>(*literal)) {
      // A declaration without an initializer still defines an own undefined
      // data property. Omit only this inert literal, never a read/conversion;
      // retain its provenance as a zero-width event at the field's name.
      Result().expressions[node.value.value] = {written_, written_};
      return;
    }
    Append(" = ");
    Push(ExpressionOperand{node.value, {ExpressionGrammar::kAssignmentValue}, node.name});
  }
  void Expression(const AssignmentExpr& node, bool statement_start = false) {
    const auto* symbol = std::get_if<SymbolTarget>(&node.target);
    Push(ExpressionOperand{node.value, {ExpressionGrammar::kAssignmentValue}, symbol ? NameContext{Tree().symbols[Initializers().Storage(symbol->symbol).value].name} : NameContext{}});
    Push(std::string_view(" = "));
    if (symbol) Push(SymbolName{symbol->symbol});
    else {
      const auto& member = std::get<MemberTarget>(node.target);
      if (node.lookup == analysis::PropertyLookup::kSuper)
        SuperMember(MemberExpr{member.base, member.key, node.lookup});
      else Member(member.base, member.key, PostfixContext::kMemberBase, false, statement_start);
    }
  }
  bool PlainValueCallee(ExprId id) {
    // Validated ScopeKind has no object/with alternative, and strict source
    // cannot introduce with. These declarative/module/global references supply
    // undefined as call receiver (ECMA-262 EvaluateCall/WithBaseObject).
    // Do not infer this from an opcode, or from the pre-projection spelling.
    if (Tree().strictness != Strictness::kStrict ||
        Initializers().transports[id.value] || Initializers().replacements[id.value]) return false;
    const auto& expression = Tree().expressions[id.value].expression;
    if (const auto* read = std::get_if<SymbolExpr>(&expression)) {
      if (Initializers().ReadLiteral(id, read->symbol)) return false;
      const auto binding = Initializers().ReadBinding(id, read->symbol);
      const auto storage = Initializers().Storage(binding);
      return Tree().names[Tree().symbols[storage.value].name.value] != u"eval";
    }
    if (const auto* global = std::get_if<GlobalNameExpr>(&expression))
      return Tree().names[global->name.value] != u"eval";
    return false;
  }
  void Expression(const CallExpr& node, bool statement_start = false) {
    if(!node.array_arguments&&node.arguments.count) {
      const auto object=Tree().arguments[node.arguments.first];
      if(const auto* site=std::get_if<TemplateObjectExpr>(&Tree().expressions[object.value].expression)) {
        if(!site->quasis||site->quasis->size()!=node.arguments.count||!TaggedTemplates(Tree().identity.contract))throw Error::kUnsupportedContext;
        Push(EndExpression{object});
        Push(std::string_view("`"));
        for(std::size_t q=site->quasis->size();q-->0;){Step();Push(TemplateChunk{object,static_cast<std::uint32_t>(q)});if(q){Push(std::string_view("}"));Push(ExpressionOperand{Tree().arguments[node.arguments.first+q],{ExpressionGrammar::kExpression}});Push(std::string_view("${"));}}
        Push(std::string_view("`"));
        Push(TemplateStart{object});
        if(node.reference==CallReference::kValue&&!PlainValueCallee(node.callee)){Push(std::string_view(")"));Push(node.callee);Push(std::string_view("(0, "));}
        else Push(ExpressionOperand{node.callee,{PostfixContext::kCallCallee},{},ExpressionObservation::kValue,false,statement_start});
        return;
      }
    }

    if (node.reference == CallReference::kValue && !PlainValueCallee(node.callee)) {
      Append("(0, ");
      Push(std::string_view(")"));
      if (node.array_arguments) Push(ArrayArgumentCursor{*node.array_arguments});
      else Push(ArgumentCursor{node.arguments, 0});
      Push(std::string_view(")("));
      // Value projection also preserves indirect eval for an identifier named
      // eval. Never omit it merely because a lexical callee looks callable.
      Push(node.callee);
    } else {
      Push(std::string_view(")"));
      if (node.array_arguments) Push(ArrayArgumentCursor{*node.array_arguments});
      else Push(ArgumentCursor{node.arguments, 0});
      Push(std::string_view("("));
      Push(ExpressionOperand{node.callee, {PostfixContext::kCallCallee}, {}, ExpressionObservation::kValue, false, statement_start});
    }
  }
  void Expression(const ConstructExpr& node) {
    Append("new ");
    Push(std::string_view(")"));
    if (const auto* arguments = std::get_if<ArgumentRange>(&node.arguments)) Push(ArgumentCursor{*arguments, 0});
    else Push(ArrayArgumentCursor{std::get<ArrayArguments>(node.arguments).array});
    Push(std::string_view("("));
    Push(ExpressionOperand{node.constructor, {PostfixContext::kConstructor}});
  }
  void Expression(const SuperCallExpr& node) {
    if (Tree().callable != CallableKind::kDerivedConstructor || !ClassRoot() ||
        (node.source != analysis::SuperArgumentSource::kExplicit &&
         !(node.source == analysis::SuperArgumentSource::kInvocationFrame &&
           Tree().parameter_form == ParameterForm::kInvocationRest &&
           std::holds_alternative<SpreadArguments>(node.arguments))))
      throw Error::kUnsupportedContext;
    Append("super(");
    Push(std::string_view(")"));
    if (const auto* range = std::get_if<ArgumentRange>(&node.arguments)) Push(ArgumentCursor{*range, 0});
    else {
      Append("...");
      Push(ExpressionOperand{std::get<SpreadArguments>(node.arguments).iterable, {ExpressionGrammar::kAssignmentValue}});
    }
  }
  void Expression(EmptyArrayExpr) { Append("[]"); }
  void Expression(ArrayExpr node) {
    Append("["); Push(std::string_view("]"));
    if (node.elements.count && std::holds_alternative<ArrayElision>(
        std::get<ArrayElement>(Tree().expressions[Tree().arguments[node.elements.first + node.elements.count - 1].value].expression).element))
      Push(std::string_view(","));
    Push(ArgumentCursor{node.elements, 0});
  }
  void Expression(const ArrayElement& node) {
    if (const auto* value = std::get_if<ArrayValueElement>(&node.element)) Push(ExpressionOperand{value->value, {ExpressionGrammar::kAssignmentValue}});
    else if (const auto* spread = std::get_if<ArraySpreadElement>(&node.element)) {
      Append("..."); Push(ExpressionOperand{spread->iterable, {ExpressionGrammar::kAssignmentValue}});
    }
  }
  void Expression(EmptyObjectExpr, bool grouped = true) { Append(grouped ? "({})" : "{}"); }
  void Expression(const ClassExpr& node, ExprId expression, NameContext inferred_name, const ExpressionOperand& operand) {
    if(!concurrent_frames_.empty()&&concurrent_frames_.back().proof) {
      ConcurrentFailure e;e.kind=ConcurrentError::kRequestUses;e.method=Tree().method;ConcurrentRefusal(e);
    }

    if (!node.definition) throw Error::kUnsupportedContext;
    // Identifier initialization/assignment performs NamedEvaluation without
    // adding an immutable inner class binding. Other naming contexts use the
    // common lexical plan, which proves no actual reference is shadowed.
    const bool property = std::holds_alternative<PropertyInferredName>(inferred_name);
    if (property && (!node.definition->name.empty() || node.definition->initializer_site))
      throw Error::kUnsupportedContext;
    const bool inferred = property || ContextName(inferred_name) == node.definition->name;
    const auto name = Names().Lookup(*node.definition); Step(name.work);
    const bool known = !std::holds_alternative<std::monostate>(operand.context.parent);
    const bool grouped = ReadableGrouping(TreeExpression{node},
        {operand.context, operand.statement_start}, ExpressionHeadFacts(TreeExpression{node})).Parenthesized();
    if (inferred && known && grouped) { Append("("); Push(std::string_view(")")); }
    ClassCreation(node, expression, inferred ? ClassPrintForm::kInferredExpression :
        name.named ? (known && !grouped ? ClassPrintForm::kBareNamedExpression : ClassPrintForm::kNamedExpression) : ClassPrintForm::kExpression);
  }
  void ClassCreation(const ClassExpr& node, ExprId expression, ClassPrintForm form) {
    if (node.definition && node.definition->class_self &&
        node.definition->class_self->certificate) {
      const auto name = Names().Lookup(*node.definition);
      Step(name.work);
      // A certified inner reference requires an actual native name. The
      // existing name plan must still prove that adding it shadows no other
      // capture/global reference in any constructor/member/initializer family.
      if (!name.named) throw Error::kUnsupportedContext;
      if (form != ClassPrintForm::kDeclaration &&
          form != ClassPrintForm::kDerivedBody &&
          form != ClassPrintForm::kBareNamedExpression)
        form = ClassPrintForm::kNamedExpression;
    }
    // Final preparation and the family's original-barrier verification own
    // admission. Printing checks the actual creator and lexical destination;
    // a descendant is not a different class-emission language.
    if (!node.definition || !node.definition->DraftPrepared() || node.definition->identity != Tree().identity ||
        node.definition->creation.creator != Tree().method) throw Error::kUnsupportedContext;
    if (node.heritage) {
      if (!node.definition->DraftPrepared() || node.definition->callable != CallableKind::kDerivedConstructor)
        throw Error::kUnsupportedContext;
      if (form == ClassPrintForm::kNamedExpression || form == ClassPrintForm::kBareNamedExpression || form == ClassPrintForm::kDeclaration) {
        Append(form == ClassPrintForm::kNamedExpression ? "(class " : "class ");
        Identifier(node.definition->name); Append(" extends (");
      }
      else {
        if (form == ClassPrintForm::kExpression) { Append("({["); Quoted(node.definition->name); Append("]: "); }
        Append("class extends (");
      }
      Push(DerivedClassBody{node.definition, expression, form}); Push(*node.heritage);
    } else ClassPart(*node.definition, form, expression);
  }
  void Run(const DerivedClassBody& body) {
    Append(")");
    ClassPart(*body.definition, ClassPrintForm::kDerivedBody, body.expression);
    if (body.form == ClassPrintForm::kNamedExpression) Append(")");
    else if (body.form == ClassPrintForm::kExpression) { Append("})["); Quoted(body.definition->name); Append("]"); }
  }
  void ClassPart(const ClassTree& definition, ClassPrintForm form, ExprId expression) {
    const ClassCaptureDestination parent{Tree(), expression};
    auto emitted = PrintClassPart(definition, output_, {limit_ - written_}, remaining_, scope_, depth_, form, &parent, &Names());
    if (const auto* error = std::get_if<TreePrintFailure>(&emitted)) throw *error;
    if (const auto* failure = std::get_if<FamilyPrepareFailure>(&emitted)) {
      switch (FamilyRouteFailure(*failure).failure_class) {
        case RouteFailureClass::kMemoryLimit: throw Error::kResourceLimit;
        case RouteFailureClass::kWorkLimit: throw Error::kWorkLimit;
        case RouteFailureClass::kOutputFailure: throw Error::kWrite;
        case RouteFailureClass::kUnsupported: case RouteFailureClass::kUnproved:
        case RouteFailureClass::kInvalidInput: case RouteFailureClass::kInvariantViolation:
          throw Error::kUnsupportedContext;
      }
    }
    auto positions = std::get<PrintedClassTree>(std::move(emitted));
    Result().incomplete |= positions.incomplete;
    Result().arkts |= positions.arkts;
    Admit(positions.allocated_bytes, 1); Step(positions.work);
    const auto retain = [&](PrintedFunction function, std::size_t offset) {
      TreePrintSiteScope site(TreePrintSite::kClassMemberRetention);
      auto& retained = Result().class_members;
      if (retained.size() == retained.capacity()) {
        const auto capacity = retained.capacity();
        if (capacity > std::numeric_limits<std::size_t>::max() / 2) {PrintCharge(TreePrintBudgetKind::kCapacityOverflow,{},remaining_.bytes,initial_.bytes,peak_bytes_);throw Error::kResourceLimit;}
        const auto next = capacity ? capacity * 2 : 1;
        Admit(next, sizeof(PrintedFunction)); retained.reserve(next);
        Admit(retained.capacity() - next, sizeof(PrintedFunction));
      }
      function.enclosing_offset = written_ + offset;
      retained.push_back(std::move(function));
    };
    for (auto& member : positions.members)
      for (auto& function : member.positions.members) retain(std::move(function), member.offset);
    if (positions.initializer) {
      const auto offset = positions.initializer->enclosing_offset;
      retain(std::move(*positions.initializer), offset);
    }
    written_ += positions.bytes;
  }
  void Expression(FunctionExpr node, bool grouped) {
    if (!family_) throw Error::kUnsupportedContext;
    const auto child = std::lower_bound(owners_.begin(), owners_.end(), node.child,
        [&](const auto& item, FunctionOwnerId owner) { Step(); return item.owner < owner; });
    if (child == owners_.end() || child->owner != node.child) throw Error::kUnsupportedContext;
    const auto parent = current_; current_ = child->member;
    Push(BeginFunction{grouped}); current_ = parent;
  }
  void Run(BeginFunction start) {
    if(Tree().sendable) {
      const auto* site=ClassRoot()?class_root_->Constructor():nullptr;
      if(!site || site->creation.kind!=abc::ClassKind::kSendable ||
          Tree().sendable!=SendableConstructorDirective{site->target,site->creator,site->pc,site->creation.constructor,site->creation.literal})
        throw Error::kUnsupportedContext;
      Result().arkts=true;
    }
    if(Tree().concurrent)ConcurrentPrint::Directive(*Tree().concurrent);
    if(Tree().concurrent || (!concurrent_frames_.empty()&&concurrent_frames_.back().proof)) {
      if(concurrent_frames_.size()==concurrent_frames_.capacity()) {
        const auto capacity=concurrent_frames_.capacity();const auto next=capacity?capacity*2:1;
        if(next<=capacity)throw Error::kResourceLimit;
        Admit(next,sizeof(ConcurrentFrame));concurrent_frames_.reserve(next);Admit(concurrent_frames_.capacity()-next,sizeof(ConcurrentFrame));
      }
      auto inherited=concurrent_frames_.empty()?std::optional<ConcurrentReferenceProof>{}:concurrent_frames_.back().proof;
      concurrent_frames_.push_back({Tree().concurrent.has_value(),Tree().concurrent?std::optional<ConcurrentReferenceProof>{*Tree().concurrent}:inherited});
      if(Tree().concurrent&&(!Tree().captures.empty()||(Tree().self&&Tree().self_form==SelfForm::kCreationBinding))) {
        ConcurrentFailure e;e.kind=ConcurrentError::kCaptures;e.method=Tree().method;ConcurrentRefusal(e);
      }
      if(family_&&concurrent_modules_.empty())Array(concurrent_modules_,family_->members.size());
      auto& modules=family_?concurrent_modules_[current_]:direct_concurrent_modules_;
      if(modules.empty()) {
        Array(modules,Tree().symbols.size());
        for(const auto& alias:Tree().modules){Step();modules[alias.symbol.value]=alias.target;}
      }
    }

    if (family_) {
      if (printed_[current_]) throw Error::kUnsupportedContext;
      printed_[current_] = true; draft_.functions[current_].begin = written_;
    }
    if (Tree().ImplicitConstructor()) {
      if (!ClassRoot() || !class_root_->Constructor() || Tree().callable != CallableKind::kDerivedConstructor ||
          Result().incomplete || !Tree().captures.empty() || !Tree().modules.empty()) throw Error::kUnsupportedContext;
      const auto begin = written_;
      Run(Indent{depth_}); Append("/* implicit default constructor */");
      const GeneratedRange range{begin, written_};
      for (auto& at : Result().parameters) at = range;
      for (auto& at : Result().expressions) at = range;
      for (auto& at : Result().statements) at = range;
      for (auto& at : Result().anchors) at = begin;
      Run(EndFunction{start.grouped}); return;
    }
    if (Result().incomplete) {
      Append("/* INCOMPLETE: partial decompilation; not executable-equivalent. */");
      // Nested function expressions can follow return/throw. A newline inside
      // this annotation would insert a semicolon before the recovered value.
      Append(family_ && current_ != family_->root.value ? " " : "\n");
    }
    if (ModuleBody()) {
      Push(EndFunction{start.grouped}); Push(Statement{Tree().body, depth_}); return;
    }
    if (ClassRoot()) {
      Run(Indent{depth_});
      if (!class_root_->Constructor() && class_root_->Placement() == abc::ClassPlacement::kStatic) Append("static ");
      if (Tree().callable == CallableKind::kAsyncMethod) Append("async ");
      if (IsGeneratorCallable(Tree().callable)) Append("*");
      if (const auto* accessor = class_root_->DefinedMember(); accessor && accessor->kind != abc::ClassMethodKind::kOrdinary) {
        if (Tree().callable != CallableKind::kMethod || Tree().parameters.size() !=
            (accessor->kind == abc::ClassMethodKind::kGetter ? 0U : 1U)) throw Error::kUnsupportedContext;
        Append(accessor->kind == abc::ClassMethodKind::kGetter ? "get " : "set ");
      }
      Key(class_root_->Name()); Append("(");
    } else if (Tree().self && Tree().self_form == SelfForm::kNamedFunction) {
      if (start.grouped) Append("(");
      Append(Tree().callable == CallableKind::kAsyncGenerator ? "async function* " :
          Tree().callable == CallableKind::kGenerator ? "function* " :
          Tree().callable == CallableKind::kAsyncFunction ? "async function " : "function ");
      Name(*Tree().self); Append("(");
    } else if (Tree().inferred_name && !NativeName()) {
      if (Tree().self) {
        // The private const is initialized before the target can be invoked.
        // An arrow envelope preserves enclosing lexical this/arguments/target.
        Append("(() => { const "); Name(*Tree().self); Append(" = ");
      }
      // NamedEvaluation, not a new lexical self scope. Computed keys preserve
      // even an empty name or __proto__, without Object.defineProperty calls.
      // A native async method supplies this VM invocation's arguments without
      // introducing an extra promise/await turn; lexical receiver reads remain
      // explicit capture bindings, as required by the arrow ABI checks.
      // This object is the name-preserving member base. In a proven value
      // position its braces need no envelope; self-binding IIFEs keep theirs.
      if(start.grouped || Tree().self)Append("(");
      Append(Tree().invocation == InvocationForm::kArrowMethod && IsAsyncCallable(Tree().callable)
          ? "{async [" : "{[");
      Quoted(Tree().names[Tree().inferred_name->value]);
      if (Tree().invocation == InvocationForm::kArrowMethod) Append("](");
      else {
        Append("]: ");
        Append(Tree().callable == CallableKind::kAsyncArrow ? "async (" : Tree().callable == CallableKind::kArrow ? "(" :
            Tree().callable == CallableKind::kAsyncGenerator ? "async function*(" :
            Tree().callable == CallableKind::kGenerator ? "function*(" :
            Tree().callable == CallableKind::kAsyncFunction ? "async function(" : "function(");
      }
    } else {
      if (start.grouped) Append("(");
      Append(Tree().callable == CallableKind::kAsyncArrow ? "async (" : Tree().callable == CallableKind::kArrow ? "(" :
          Tree().callable == CallableKind::kAsyncGenerator ? "async function*(" :
          Tree().callable == CallableKind::kGenerator ? "function*(" :
          Tree().callable == CallableKind::kAsyncFunction ? "async function(" : "function(");
    }
    Push(EndFunction{start.grouped}); Push(Statement{Tree().body, depth_});
    Push(std::string_view(IsArrowCallable(Tree().callable) &&
        Tree().invocation == InvocationForm::kDeclared ? ") => " : ") "));
    Push(ParameterCursor{0});
  }
  void Run(EndFunction end) {
    if(!concurrent_frames_.empty()&&concurrent_frames_.back().proof) {
      auto frame=concurrent_frames_.back();concurrent_frames_.pop_back();
      if(frame.owns){if(const auto failure=frame.proof->Finish())ConcurrentRefusal(*failure);}
      else if(!concurrent_frames_.empty())concurrent_frames_.back().proof=frame.proof;
    }

    for (std::size_t i = 0; i < Tree().anchors.size(); ++i) {
      Step(); if (std::holds_alternative<FunctionExit>(Tree().anchors[i].position)) Result().anchors[i] = written_;
    }
    // Class-body syntax closes with the block itself, not an expression wrap.
    if (!ClassRoot() && !ModuleBody()) {
      if (Tree().inferred_name && !NativeName() && (!Tree().self || Tree().self_form == SelfForm::kCreationBinding)) {
        Append(end.grouped || Tree().self ? "})[" : "}["); Quoted(Tree().names[Tree().inferred_name->value]); Append("]");
        if (Tree().self) { Append("; return "); Name(*Tree().self); Append("; })()"); }
      } else if (end.grouped) Append(")");
    }
    if (family_) {
      draft_.functions[current_].end = written_;
      Result().bytes = written_ - draft_.functions[current_].begin;
    }
  }
  void Run(ParameterCursor cursor) {
    if (cursor.index == Tree().parameters.size()) return;
    if (cursor.index) Append(", ");
    const auto& parameter = Tree().parameters[cursor.index];
    Result().parameters[cursor.index].begin = written_;
    if (parameter.rest) Append("...");
    Name(parameter.symbol);
    Push(ParameterCursor{cursor.index + 1}); Push(EndParameter{cursor.index});
    if (parameter.initializer) {
      if ((!Tree().parameter_expressions &&
           !std::holds_alternative<TreeLiteral>(
               Tree().expressions[parameter.initializer->value].expression) &&
           !std::holds_alternative<SymbolExpr>(
               Tree().expressions[parameter.initializer->value].expression)))
        throw Error::kUnsupportedContext;
      Append(" = "); Push(ExpressionOperand{*parameter.initializer,{ExpressionGrammar::kAssignmentValue}});
    }
  }
  void Run(EndParameter end) { Result().parameters[end.index].end = written_; }
  void Expression(ObjectExpr node, bool grouped = true) {
    const bool array = node.kind == AggregateKind::kArray;
    bool multiline = false;
    // Layout follows typed literal nesting, never sample names or printed text.
    // Scan each owned property once; the existing iterative action stack keeps
    // child ownership, origins, indentation and output/work budgets intact.
    for (std::uint32_t i = 0; i < node.properties.count; ++i) {
      Step();
      const auto value = Tree().properties[node.properties.first + i].value;
      const auto* child = std::get_if<ObjectExpr>(&Tree().expressions[value.value].expression);
      multiline |= child && child->properties.count != 0;
    }
    Append(array ? "[" : grouped ? "({" : "{");
    Push(std::string_view(array ? "]" : grouped ? "})" : "}"));
    if (multiline) {
      Push(Indent{depth_}); Push(std::string_view("\n"));
      Append("\n"); ++depth_;
    }
    Push(ObjectCursor{node.properties, 0, node.kind, multiline});
  }
  void Run(ObjectCursor cursor) {
    if (cursor.index == cursor.range.count) return;
    if (cursor.index != 0) Append(cursor.multiline ? ",\n" : ", ");
    if (cursor.multiline) Run(Indent{depth_});
    const PropertyId id{cursor.range.first + cursor.index};
    const auto& property = Tree().properties[id.value];
    Result().properties[id.value].begin = written_;
    if(!property.key)Append("...");
    else if (cursor.kind == AggregateKind::kObject) {
      const auto& name = Tree().names[property.key->value];
      Step(name.size());
      // Ordinary IdentifierName keys keep native object syntax. __proto__ must
      // remain computed: a colon definition otherwise mutates the prototype.
      if (ClassifyObjectKey(name) == MemberKeySyntax::kIdentifier) {
        Identifier(name); Append(": ");
      } else {
        Append("["); Quoted(name); Append("]: ");
      }
    }
    Push(ObjectCursor{cursor.range, cursor.index + 1, cursor.kind, cursor.multiline});
    Push(EndProperty{id}); Push(ExpressionOperand{property.value, {ExpressionGrammar::kAssignmentValue},
        cursor.kind == AggregateKind::kObject && property.key ? (property.name_setting == analysis::PropertyNameSetting::kPropertyKey ?
                NameContext{PropertyInferredName{*property.key}} : NameContext{*property.key}) : NameContext{}});
  }
  void Run(EndProperty property) { Result().properties[property.id.value].end = written_; }
  void Run(ArgumentCursor cursor) {
    if (cursor.index == cursor.range.count) return;
    if (cursor.index != 0) Append(", ");
    Push(ArgumentCursor{cursor.range, cursor.index + 1});
    Push(ExpressionOperand{Tree().arguments[cursor.range.first + cursor.index], {ExpressionGrammar::kAssignmentValue}});
  }
  void Run(ArrayArgumentCursor cursor) {
    // A checked private array is an argument carrier, never a spread iterable.
    // Keep its origin span and each property's span while omitting only the
    // unobservable allocation; resource failures are excluded by tree admission.
    Result().expressions[cursor.array.value].begin = written_;
    Push(EndExpression{cursor.array});
    const auto& syntax = Tree().expressions[cursor.array.value].expression;
    if (std::holds_alternative<EmptyArrayExpr>(syntax)) return;
    if (const auto* elements = std::get_if<ArrayExpr>(&syntax)) {
      Push(ArgumentCursor{elements->elements, 0}); return;
    }
    const auto* array = std::get_if<ObjectExpr>(&syntax);
    if (!array || array->kind != AggregateKind::kArray) throw Error::kUnsupportedContext;
    Push(ObjectCursor{array->properties, 0, AggregateKind::kArray});
  }
  void Run(SnapshotCursor cursor) {
    if (cursor.index == cursor.range.count) return;
    if (cursor.index != 0) Append(", ");
    Append("(arguments.length > "); Numeric(cursor.index); Append(" ? ");
    Push(SnapshotCursor{cursor.range, cursor.index + 1});
    Push(std::string_view(" : void 0)"));
    Push(ExpressionOperand{Tree().arguments[cursor.range.first + cursor.index], {ExpressionGrammar::kAssignmentValue}});
  }
  void Run(TemplateStart start) {
    Result().expressions[start.object.value].begin = written_;
    Result().expression_extents[start.object.value] = GeneratedExtent::kSpan;
  }
  void Run(TemplateChunk chunk) {
    const auto& site=std::get<TemplateObjectExpr>(Tree().expressions[chunk.object.value].expression);
    const auto& raw=Tree().texts[(*site.quasis)[chunk.index].raw.value];
    for(std::size_t i=0;i<raw.size();++i){Step();std::uint32_t cp=raw[i];if(cp>=0xd800&&cp<=0xdbff){if(++i>=raw.size()||raw[i]<0xdc00||raw[i]>0xdfff)throw Error::kInvalidLiteral;cp=0x10000+((cp-0xd800)<<10)+(raw[i]-0xdc00);}else if(cp>=0xdc00&&cp<=0xdfff)throw Error::kInvalidLiteral;
      char bytes[4];std::size_t size=0;if(cp<0x80)bytes[size++]=char(cp);else if(cp<0x800){bytes[size++]=char(0xc0|(cp>>6));bytes[size++]=char(0x80|(cp&63));}else if(cp<0x10000){bytes[size++]=char(0xe0|(cp>>12));bytes[size++]=char(0x80|((cp>>6)&63));bytes[size++]=char(0x80|(cp&63));}else{bytes[size++]=char(0xf0|(cp>>18));bytes[size++]=char(0x80|((cp>>12)&63));bytes[size++]=char(0x80|((cp>>6)&63));bytes[size++]=char(0x80|(cp&63));}Append({bytes,size});}
  }
  void Run(std::string_view token) { Append(token); }
  void Run(SymbolName symbol) { Name(symbol.id); }
  void Run(PropertyKey key) {
    const auto& name = Tree().names[key.id.value];
    Step(name.size());
    if (IsSourceIdentifier(name)) { Append(key.optional ? "?." : "."); Identifier(name); }
    else { Append(key.optional ? "?.[" : "["); Quoted(name); Append("]"); }
  }
  void Run(ExprId id) { Run(ExpressionOperand{id, {}}); }
  void BeginExpression(ExprId id) {
    Result().expressions[id.value].begin = written_;
    Result().expression_extents[id.value] = GeneratedExtent::kSpan;
    Push(EndExpression{id});
  }
  std::optional<bool> BooleanLiteralValue(ExprId id) const {
    const auto* literal = std::get_if<TreeLiteral>(&Tree().expressions[id.value].expression);
    const auto* boolean = literal ? std::get_if<BooleanLiteral>(literal) : nullptr;
    return boolean ? std::optional{boolean->value} : std::nullopt;
  }
  bool BooleanChoice(ConditionalExpr node, ExpressionOperand operand, bool inverted) {
    const auto yes = BooleanLiteralValue(node.consequent);
    const auto no = BooleanLiteralValue(node.alternate);
    if (!yes || !no) return false;
    // The omitted literal branches retain ranges covering their normalized
    // expression. Constant selections still evaluate the original condition;
    // neither its getter/throw nor its GetValue may disappear.
    BeginExpression(node.consequent); BeginExpression(node.alternate);
    if (operand.observation == ExpressionObservation::kDiscarded) {
      operand.id = node.condition; operand.inferred_name = {}; Push(operand);
    } else if (*yes == *no) {
      Append("(");
      Push(std::string_view((*yes != inverted) ? ", true)" : ", false)"));
      Push(ExpressionOperand{node.condition, {}, {}, ExpressionObservation::kDiscarded});
    } else {
      operand.id = node.condition; operand.inferred_name = {};
      Push(BooleanOperand{operand, inverted != !*yes});
    }
    return true;
  }
  void Run(BooleanOperand operand) {
    const auto id = operand.expression.id;
    const auto& expression = Tree().expressions[id.value].expression;
    if(operand.inverted && operand.expression.observation!=ExpressionObservation::kDiscarded &&
        Variables().readable_equalities[id.value] &&
        *Variables().readable_equalities[id.value]!=id) {
      // Preserve the snapshot's occurrence/position ownership, but carry NOT
      // polarity to the final printed inequality before emitting any tokens.
      BeginExpression(id);
      const auto next=Initializers().replacements[id.value];
      if(!next)throw Error::kUnsupportedContext;
      Push(ProjectElidedOperands{id,*next});
      operand.expression.id=*next;operand.expression.inferred_name={};
      operand.expression.value_result=true;Push(operand);return;
    }
    if (const auto* unary = std::get_if<UnaryExpr>(&expression);
        unary && unary->operation == TreeUnary::kNot) {
      BeginExpression(id);
      operand.expression.id = unary->operand;
      operand.inverted = !operand.inverted;
      Push(operand); return;
    }
    if (const auto* conditional = std::get_if<ConditionalExpr>(&expression);
        conditional && BooleanLiteralValue(conditional->consequent) && BooleanLiteralValue(conditional->alternate)) {
      BeginExpression(id);
      BooleanChoice(*conditional, operand.expression, operand.inverted);
      return;
    }
    if (const auto* binary = std::get_if<BinaryExpr>(&expression))
      if (const auto complement = TreeEqualityComplement(binary->operation)) {
        BeginExpression(id);
        const auto operation = operand.inverted && operand.expression.observation != ExpressionObservation::kDiscarded ?
            *complement : binary->operation;
        const BinaryExpr normalized{operation, binary->left, binary->right};
        if (TreeOperandGrouping(TreeExpression{normalized}, operand.expression.context)) {
          Append("("); Push(std::string_view(")"));
        }
        Expression(normalized, operand.expression.statement_start); return;
      }
    // NOT/Boolean choices compose as a polarity. ToBoolean has no user hook:
    // emit at most one NOT, or two for a positive exact Boolean value. A pure
    // truthiness observation may expose the child with its real parent grammar.
    if (operand.expression.observation != ExpressionObservation::kDiscarded &&
        (operand.inverted || operand.expression.observation == ExpressionObservation::kValue)) {
      if (TreeOperandGrouping(TreeExpression{UnaryExpr{TreeUnary::kNot, id}}, operand.expression.context)) {
        Append("("); Push(std::string_view(")"));
      }
      Append(operand.inverted ? "!" : "!!");
      Push(ExpressionOperand{id, {TreeUnary::kNot}, {}, ExpressionObservation::kTruthiness});
    } else Push(operand.expression);
  }
  void Run(PostfixSpine spine) {
    BeginExpression(spine.expression);
    const auto& expression = Tree().expressions[spine.expression.value].expression;
    if (const auto* member = std::get_if<MemberExpr>(&expression)) {
      if (spine.expression == spine.access) Member(spine.base, member->key, PostfixContext::kMemberBase, true);
      else {
      // Keep later members in this same native chain. Grouping between links
      // would end short-circuiting; making every link optional would hide the
      // original exception. Every node retains its own begin/end event.
        PrintMemberKey(member->key, false); Push(PostfixSpine{member->base, spine.access, spine.base});
      }
    } else {
      const auto& call = std::get<CallExpr>(expression);
      // Keep each original argument occurrence and callee-before-arguments
      // order. Only the first guarded invocation is optional/indirect; later
      // value calls consume a call's value, while member calls keep Reference.
      Push(std::string_view(")")); Push(ArgumentCursor{call.arguments,0});
      Push(std::string_view(spine.expression == spine.access ? "?.(" : "("));
      if (spine.expression == spine.access) Push(ExpressionOperand{spine.base,{PostfixContext::kCallCallee}});
      else Push(PostfixSpine{call.callee,spine.access,spine.base});
    }
  }
  void Run(ExpressionOperand operand) {
    const auto id = operand.id;
    BeginExpression(id);
    if (!Nullish().projections.empty() && Nullish().projections[id.value]) {
      const auto& projection=*Nullish().projections[id.value];
      // Optional syntax is a LeftHandSideExpression. A following postfix use
      // must still terminate the chain; binary/argument/value contexts do not.
      const auto syntax=std::visit([&](const auto& part)->TreeExpression {
        using T=std::decay_t<decltype(part)>;
        if constexpr(std::is_same_v<T,NullishFallbackSyntax>)
          return LogicalExpr{TreeLogical::kNullish,part.value,part.fallback};
        else if constexpr(std::is_same_v<T,OptionalMemberSyntax>)
          return Tree().expressions[part.member.value].expression;
        else return Tree().expressions[part.call.value].expression;
      },projection);
      const bool boundary=operand.statement_start ||
          std::holds_alternative<PostfixContext>(operand.context.parent);
      if(ReadableGrouping(syntax,{operand.context,false,false,true,false,boundary}).Parenthesized()) {
        Append("("); Push(std::string_view(")"));
      }
      PrintNullish(projection,operand.observation);
      return;
    }
    PrintOriginalExpression(operand);
  }
  void PrintNullish(const NullishProjection& projection,ExpressionObservation observation) {
      std::visit([&](const auto& syntax) {
        using T = std::decay_t<decltype(syntax)>;
        if constexpr (std::is_same_v<T, OptionalMemberSyntax>) {
          Push(PostfixSpine{syntax.member, syntax.access, syntax.base});
        } else if constexpr (std::is_same_v<T, OptionalCallSyntax>) {
          const auto& call = std::get<CallExpr>(Tree().expressions[syntax.call.value].expression);
          BeginExpression(syntax.call);
          Push(std::string_view(")")); Push(ArgumentCursor{call.arguments, 0});
          Push(std::string_view(syntax.prefix ? "(" : "?.("));
          if (syntax.prefix) {
            Push(PostfixSpine{*syntax.prefix, *syntax.access, syntax.base});
          } else Push(ExpressionOperand{syntax.base, {PostfixContext::kCallCallee}});
        } else {
          static_assert(std::is_same_v<T, NullishFallbackSyntax>);
          Expression(LogicalExpr{TreeLogical::kNullish, syntax.value, syntax.fallback}, observation);
        }
      }, projection);
  }
  void PrintOriginalExpression(ExpressionOperand operand) {
    const auto id=operand.id;
    const auto& expression=Tree().expressions[id.value].expression;
    if(const auto transport=Initializers().transports[id.value]) {
      const auto& literal=Initializers().primitives[transport->replacement.value];
      if(TreeOperandGrouping(TreeExpression{literal},operand.context)){Append("(");Push(std::string_view(")"));}
      ContextLiteral(literal,operand.context);return;
    }
    if(const auto replacement=Initializers().replacements[id.value]) {
      // The planner owns elision. Transport the omitted operand's origin to
      // the projected span, regardless of side; no effect/value-use decision.
      Push(ProjectElidedOperands{id,*replacement});
      operand.id=*replacement;operand.value_result=true;Push(operand);return;
    }
    if (const auto* read = std::get_if<SymbolExpr>(&expression)) if (const auto value = Initializers().ReadLiteral(operand.id,read->symbol)) {
      const auto& literal = Initializers().primitives[value->value];
      if (TreeOperandGrouping(TreeExpression{literal},operand.context)) { Append("(");Push(std::string_view(")")); }
      ContextLiteral(literal,operand.context);return;
    }
    if(const auto* read=std::get_if<SymbolExpr>(&expression)) {
      const auto binding = Initializers().ReadBinding(operand.id,read->symbol);
      const auto storage = Initializers().Storage(binding);
      const bool protect = StatementIdentifierGrouping(Tree().names[Tree().symbols[storage.value].name.value],
          {operand.context, operand.statement_start});
      if (protect) Append("(");
      Name(binding);
      if (protect) Append(")");
      return;
    }
    if (const auto* global = std::get_if<GlobalNameExpr>(&expression)) {
      if (StatementIdentifierGrouping(Tree().names[global->name.value], {operand.context, operand.statement_start})) {
        Append("("); Expression(*global); Append(")"); return;
      }
    }
    const auto* unary_parent = std::get_if<TreeUnary>(&operand.context.parent);
    if (operand.value_result &&
        ((!std::holds_alternative<NoInferredName>(operand.inferred_name) &&
          (std::holds_alternative<FunctionExpr>(expression) || std::holds_alternative<ClassExpr>(expression))) ||
         (unary_parent && *unary_parent == TreeUnary::kTypeof && std::holds_alternative<GlobalNameExpr>(expression)))) {
      Append("(0, "); Push(std::string_view(")"));
      operand.context = {}; operand.inferred_name = {};
    }
    if (const auto* unary = std::get_if<UnaryExpr>(&expression);
        unary && unary->operation == TreeUnary::kNot) {
      operand.id = unary->operand; operand.inferred_name = {};
      Push(BooleanOperand{operand, true}); return;
    }
    if (const auto* conditional = std::get_if<ConditionalExpr>(&expression);
        conditional && BooleanChoice(*conditional, operand, false)) return;
    // Group only after all value/name/Boolean projections above. Uncovered
    // contexts retain the node's old grouping. No reference wrapper is removed.
    const bool readable = std::holds_alternative<AwaitExpr>(expression) ||
        std::holds_alternative<SequenceExpr>(expression) ||
        std::holds_alternative<UpdateExpr>(expression) ||
        std::holds_alternative<ConditionalExpr>(expression) ||
        std::holds_alternative<AssignmentExpr>(expression) ||
        std::holds_alternative<FunctionExpr>(expression) ||
        std::holds_alternative<EmptyObjectExpr>(expression) ||
        (std::get_if<ObjectExpr>(&expression) && std::get<ObjectExpr>(expression).kind == AggregateKind::kObject);
    const bool grouped = readable ? ReadableGrouping(expression, {operand.context, operand.statement_start}, ExpressionHeadFacts(expression)).Parenthesized() :
        TreeOperandGrouping(expression, operand.context);
    if (grouped && !std::holds_alternative<FunctionExpr>(expression)) { Append("("); Push(std::string_view(")")); }
    if (grouped) operand.statement_start = false;
    std::visit([&](const auto& node) {
      if constexpr (std::is_same_v<std::decay_t<decltype(node)>, ClassExpr>) Expression(node, id, operand.inferred_name, operand);
      else if constexpr (std::is_same_v<std::decay_t<decltype(node)>, LogicalExpr> ||
          std::is_same_v<std::decay_t<decltype(node)>, ConditionalExpr> ||
          std::is_same_v<std::decay_t<decltype(node)>, SequenceExpr>) Expression(node, operand.observation, operand.statement_start);
      else if constexpr (std::is_same_v<std::decay_t<decltype(node)>, ObjectExpr> ||
          std::is_same_v<std::decay_t<decltype(node)>, EmptyObjectExpr>) {
        Expression(node, false);
      }
      else if constexpr (std::is_same_v<std::decay_t<decltype(node)>, MemberExpr>) {
        if (node.lookup == analysis::PropertyLookup::kSuper) { SuperMember(node); return; }
        const auto* context = std::get_if<PostfixContext>(&operand.context.parent);
        Member(node.base, node.key, context && *context == PostfixContext::kConstructor ?
            PostfixContext::kConstructor : PostfixContext::kMemberBase, false, operand.statement_start);
      }
      else if constexpr (std::is_same_v<std::decay_t<decltype(node)>, FunctionExpr>) Expression(node, grouped);
      else if constexpr (std::is_same_v<std::decay_t<decltype(node)>, BinaryExpr> ||
          std::is_same_v<std::decay_t<decltype(node)>, AssignmentExpr> ||
          std::is_same_v<std::decay_t<decltype(node)>, CallExpr>) Expression(node, operand.statement_start);
      else if constexpr (std::is_same_v<std::decay_t<decltype(node)>, TreeLiteral>) ContextLiteral(node,operand.context);
      else Expression(node);
    }, Tree().expressions[id.value].expression);
  }
  void Run(EndExpression end) { Result().expressions[end.id.value].end = written_; }
  void Run(ProjectElidedOperands projection) {
    // Normalize origins of every omitted owned operand, independent of the
    // expression's grammar. Retained descendants keep their actual spans.
    // The committed replacement alone selects execution; printing adds none.
    VisitExpressionChildren(Tree(),Tree().expressions[projection.expression.value].expression,[&](ExprId child) {
      Push(ProjectedExpression{child,Result().expressions[projection.expression.value].begin,written_,GeneratedExtent::kSpan,projection.retained});
    });
  }
  void Run(ProjectedExpression projection) {
    Step();if(projection.retained==projection.id)return;
    // Transport omitted origins to a normalized span or explicit boundary.
    // A moved subtree belongs to its retained destination. Keep identities
    // and extent tags; never fabricate a PC or execute the omitted subtree.
    Result().expressions[projection.id.value] = {projection.begin, projection.end};
    Result().expression_extents[projection.id.value] = projection.extent;
    const auto replacement = Initializers().replacements[projection.id.value];
    bool owned_replacement = false;
    VisitExpressionChildren(Tree(), Tree().expressions[projection.id.value].expression, [&](ExprId child) {
      owned_replacement |= replacement == child;
      // Its RHS was moved to a retained read. The omitted source wrapper must
      // not overwrite that destination span, including omitted parent sequences.
      if(const auto transport=Initializers().transports[projection.id.value];transport&&transport->value==child)return;
      Push(ProjectedExpression{child, projection.begin, projection.end, projection.extent, projection.retained});
    });
    // A forwarded RHS has left its original omitted statement. If its final
    // destination is itself omitted, retain its origin at this boundary too.
    // Direct RHS replacements are already owned children; do not visit twice.
    if (replacement && !owned_replacement)
      Push(ProjectedExpression{*replacement, projection.begin, projection.end, projection.extent, projection.retained});
  }
  void Run(Indent indent) {
    constexpr std::string_view kIndent = "  ";
    for (std::size_t i = 0; i < indent.depth; ++i) Append(kIndent);
  }
  void ProjectDeclarationOrigins(StmtId statement,std::optional<ExprId> retained) {
    VisitStatementExpressions(Tree().statements[statement.value].statement,[&](ExprId expression) {
      if(retained==expression)return;
      Push(ProjectedExpression{expression,written_,written_,GeneratedExtent::kBoundary,retained});
    });
  }
  void PrintCompletion(const DeclarationCompletion& completion,StmtId statement,bool declarator=false,bool header=false) {
    ProjectDeclarationOrigins(statement,completion.evaluation);
    if(declarator) {
      Name(completion.binding);
      if(completion.evaluation) {
        Append(" = (");Push(std::string_view(", void 0)"));
        Push(ExpressionOperand{*completion.evaluation,{}, {},ExpressionObservation::kDiscarded});
      }else if(completion.declaration==DeclarationKind::kConst)Append(" = void 0");
      return;
    }
    if(header) {
      if(completion.evaluation) {
        Append("(");Push(std::string_view(")"));
        Push(std::string_view(" = void 0"));Push(SymbolName{completion.binding});Push(std::string_view(", "));
        Push(ExpressionOperand{*completion.evaluation,{}, {},ExpressionObservation::kDiscarded});
      }else {Name(completion.binding);Append(" = void 0");}
      return;
    }
    Push(std::string_view(";"));
    if(completion.kind==DeclarationCompletionKind::kAssignUndefined || completion.declaration==DeclarationKind::kConst)
      Push(std::string_view(" = void 0"));
    Push(SymbolName{completion.binding});
    if(completion.kind==DeclarationCompletionKind::kInitialize)
      Push(std::string_view(completion.declaration==DeclarationKind::kConst?"const ":"let "));
    if(completion.evaluation) {
      Push(Indent{depth_});Push(std::string_view(";\n"));
      Push(ExpressionOperand{*completion.evaluation,{}, {},ExpressionObservation::kDiscarded});
    }
  }
  void PrintDeclarationValue(const DeclarationValue& value,StmtId statement) {
    ProjectDeclarationOrigins(statement,value.value);Push(std::string_view(";"));
    if(value.target) {
      Name(*value.target);Append(" = ");
      Push(ExpressionOperand{value.value,{ExpressionGrammar::kAssignmentValue},
          Tree().symbols[Initializers().Storage(*value.target).value].name});
    }else Push(value.value);
  }
  void StatementNode(const BlockStmt& node, Statement at) {
    if(at.block!=BlockForm::kBraced){Push(BlockCursor{at.id,0,at.depth,at.block});return;}
    if (node.label) { Label(*node.label); Append(": "); }
    if (!ModuleBody() || at.id != Tree().body) Append("{\n");
    if(at.id==Tree().body && Tree().concurrent) {
      Run(Indent{at.depth+1});Append("\"use concurrent\";\n");
    }
    if(at.id==Tree().body && Tree().sendable) {
      Run(Indent{at.depth+1});Append("\"use sendable\";\n");
    }
    Push(BlockCursor{at.id, 0, at.depth});
  }
  void StatementNode(const DeclarationStmt& node, Statement at) {
    if(const auto target=Initializers().undefined_stores[at.id.value]) {
      Name(*target);Append(" = void 0;");return;
    }
    if (ModuleBody() && default_export_ && at.id == default_export_->declaration) {
      Append("export default ");
      const auto& value = Tree().expressions[node.initializer->value].expression;
      const bool creation = std::holds_alternative<ClassExpr>(value);
      // Keep AssignmentExpression grammar. A bare function declaration would
      // initialize its export during linking; a named class declaration would
      // add an outer lexical binding. Parentheses preserve default naming and
      // original creation time without either extra binding or hoisting. The
      // common function-expression printer already supplies its own grouping.
      if (creation) Append("(");
      Push(std::string_view(creation ? ");" : ";"));
      Push(ExpressionOperand{*node.initializer, {}, DefaultInferredName{}});
      return;
    }
    const auto& binding = Tree().symbols[node.symbol.value];
    if (node.initializer && binding.declaration == DeclarationKind::kLet) {
      const auto* created = std::get_if<ClassExpr>(&Tree().expressions[node.initializer->value].expression);
      if (created && created->definition && Tree().names[binding.name.value] == created->definition->name) {
        const auto name = Names().Lookup(*created->definition); Step(name.work);
        if (name.named) {
          // ECMA-262 class declarations keep the writable outer let binding
          // and creation/TDZ timing. Their added immutable self scope is safe
          // only after the common lexical plan; const destinations stay const.
          // Preserve the original initializer ID and descendant source maps.
          BeginExpression(*node.initializer);
          ClassCreation(*created, *node.initializer, ClassPrintForm::kDeclaration);
          return;
        }
      }
    }
    switch (Tree().symbols[node.symbol.value].declaration) {
      case DeclarationKind::kLet: Append("let "); break;
      case DeclarationKind::kConst: Append("const "); break;
      case DeclarationKind::kParameter: case DeclarationKind::kCatchParameter:
        throw Error::kUnsupportedContext;
    }
    Name(node.symbol); Push(std::string_view(";"));
    if (node.initializer) { Append(" = "); Push(ExpressionOperand{*node.initializer, {ExpressionGrammar::kAssignmentValue}, Tree().symbols[node.symbol.value].name}); }
  }
  void Run(ObjectRestCursor at) {
    if(at.index==at.pattern->fields.size()) { Append("...");Name(at.pattern->target);return; }
    const auto& field=at.pattern->fields[at.index];const auto& member=std::get<MemberExpr>(Tree().expressions[field.expression.value].expression);
    Push(ObjectRestCursor{at.pattern,at.index+1});Push(std::string_view(", "));BeginExpression(field.expression);Push(SymbolName{field.target});Push(std::string_view(": "));
    if(const auto* key=std::get_if<NameId>(&member.key))Key(Tree().names[key->value]);
    else {const auto& rest=std::get<ObjectRestExpr>(Tree().expressions[at.pattern->expression.value].expression);const auto excluded_key=Tree().arguments[rest.keys.first+at.index];
      // The pinned frontend otherwise excludes an identifier's spelling. A
      // certified private String snapshot is inert to read twice: the value
      // sequence forces the computed-key lowering without repeating its producer.
      const bool snapshot=std::holds_alternative<SymbolExpr>(Tree().expressions[excluded_key.value].expression);
      Push(std::string_view(snapshot?")]":"]"));Push(ExpressionOperand{excluded_key,{ExpressionGrammar::kAssignmentValue}});Push(std::string_view(snapshot?"[(0, ":"["));}
  }
  void StatementNode(ExpressionStmt node, Statement at) {
    if(!Variables().object_rests.empty()&&Variables().object_rests[at.id.value]) {
      const auto& pattern=Original()->object_rests[*Variables().object_rests[at.id.value]];const auto& rest=std::get<ObjectRestExpr>(Tree().expressions[pattern.expression.value].expression);
      Append("(");Push(std::string_view(");"));BeginExpression(node.expression);BeginExpression(pattern.expression);Append("{");
      for(std::uint32_t k=0;k<rest.keys.count;++k){Step();Push(ProjectedExpression{Tree().arguments[rest.keys.first+k],written_,written_,GeneratedExtent::kBoundary,{}});}
      Push(ExpressionOperand{rest.source,{ExpressionGrammar::kAssignmentValue}});Push(std::string_view("} = "));Push(ObjectRestCursor{&pattern,0});return;
    }
    if (const auto& initializer = Initializers().statements[at.id.value]) {
      const auto& assignment = std::get<AssignmentExpr>(Tree().expressions[initializer->assignment.value].expression);
      BeginDeclarationExpression(node.expression, initializer->assignment);
      Append(initializer->declaration == DeclarationKind::kConst ? "const " : "let ");
      Name(initializer->symbol); Append(" = ");
      Push(std::string_view(";"));
      Push(ExpressionOperand{assignment.value, {ExpressionGrammar::kAssignmentValue},
          Tree().symbols[Initializers().Storage(initializer->symbol).value].name});
      return;
    }
    Push(std::string_view(";")); Push(ExpressionOperand{node.expression, {ExpressionGrammar::kExpression}, {}, ExpressionObservation::kDiscarded, false, true});
  }
  void BeginDeclarationExpression(ExprId original, ExprId retained) {
    // The declaration pass owns the retained assignment identity. Follow only
    // its committed projections to transport origins; never reselect a store
    // from the original syntax (nested dead stores exercise this boundary).
    while (original != retained) {
      Step(); BeginExpression(original);
      const auto replacement = Initializers().replacements[original.value];
      if (!replacement) throw Error::kUnsupportedContext;
      Push(ProjectElidedOperands{original,*replacement});
      original = *replacement;
    }
    BeginExpression(retained);
  }
  void StatementNode(const IfStmt& node, Statement at) {
    if(!Nullish().statement_projections.empty() && Nullish().statement_projections[at.id.value]) {
      Push(std::string_view(";"));PrintNullish(*Nullish().statement_projections[at.id.value],ExpressionObservation::kDiscarded);return;
    }
    const auto rewrite=Variables().readable.branches[at.id.value];
    Append("if (");
    if(rewrite==ReadableBranch::kInvert) {
      Push(Statement{*node.alternate,at.depth});Push(std::string_view(") "));
      Push(BooleanOperand{{node.condition,{ExpressionGrammar::kExpression},{},ExpressionObservation::kTruthiness},true});
      Push(ElideStatement{node.consequent});return;
    }
    if(rewrite==ReadableBranch::kDropElse)Push(ElideStatement{*node.alternate});
    if (node.alternate && rewrite!=ReadableBranch::kDropElse) {
      Push(Statement{*node.alternate, at.depth, Variables().braces.statements[node.alternate->value]==BraceReason::kElseIf ? BlockForm::kElseIf : BlockForm::kBraced}); Push(std::string_view(" else "));
    }
    Push(Statement{node.consequent, at.depth}); Push(std::string_view(") "));
    Push(ExpressionOperand{node.condition, {ExpressionGrammar::kExpression}, {}, ExpressionObservation::kTruthiness});
  }
  void StatementNode(const SwitchStmt& node, Statement at) {
    const auto& body =
        std::get<BlockStmt>(Tree().statements[node.body.value].statement);
    std::optional<ExprId> discriminant;
    for (std::uint32_t i = 0; i < body.child_count; ++i) {
      Step();
      const auto& clause = std::get<SwitchCaseStmt>(
          Tree()
              .statements[Tree().children[body.first_child + i].value]
              .statement);
      if (clause.condition) {
        discriminant = SwitchComparison(Tree(), clause).left;
        break;
      }
    }
    if (!discriminant) throw Error::kUnsupportedContext;
    if(const auto value=Variables().readable.switch_values[at.id.value])discriminant=value;
    if (node.label && !Variables().readable.omit_labels[at.id.value]) {
      Label(*node.label);
      Append(": ");
    }
    Append("switch (");
    Push(Statement{node.body, at.depth});
    Push(std::string_view(") "));
    Push(ExpressionOperand{*discriminant, {ExpressionGrammar::kExpression}});
  }
  void StatementNode(const SwitchCaseStmt& node, Statement at) {
    Push(Statement{node.body, at.depth});
    if (!node.condition) {
      Append("default: ");
      return;
    }
    const auto& comparison = SwitchComparison(Tree(), node);
    // The comparison is the canonical case predicate. Its cached left read is
    // printed once in the switch header; the strict-equality witness stays
    // owned.
    BeginExpression(*node.condition);
    if (const auto* unary = std::get_if<UnaryExpr>(
            &Tree().expressions[node.condition->value].expression))
      BeginExpression(unary->operand);
    if (Result().expressions[comparison.left.value].begin ==
        Result().expressions[comparison.left.value].end)
      Push(ProjectedExpression{comparison.left, written_, written_,
                               GeneratedExtent::kBoundary});
    Push(std::string_view(": "));
    Push(ExpressionOperand{comparison.right, {ExpressionGrammar::kExpression}});
    Append("case ");
  }
  void StatementNode(ReturnStmt node, Statement) {
    if (static_fields_) return;
    if (!ModuleBody()) Append("return");
    Push(std::string_view(";"));
    if (node.value) {
      if (ModuleBody()) {
        // A discarded entry completion is an expression statement. Parentheses
        // keep object literals and other leading tokens out of statement grammar.
        Append("("); Push(std::string_view(")"));
      } else Append(" ");
      Push(ExpressionOperand{*node.value, {ExpressionGrammar::kExpression}});
    }
  }
  void StatementNode(ThrowStmt node, Statement) {
    Append("throw "); Push(std::string_view(";")); Push(ExpressionOperand{node.value, {ExpressionGrammar::kExpression}});
  }
  void StatementNode(const TryStmt& node, Statement at) {
    Append("try ");
    if (node.finalizer) {
      Push(Statement{*node.finalizer, at.depth}); Push(std::string_view(" finally "));
    }
    if (node.handler) {
      Push(Statement{node.handler->body, at.depth}); Push(std::string_view(") "));
      Push(SymbolName{node.handler->parameter}); Push(std::string_view(" catch ("));
    }
    Push(Statement{node.body, at.depth});
  }
  void StatementNode(ForInStmt node, Statement at) {
    if (node.label) { Label(*node.label); Append(": "); }
    Append("for (");
    if (node.binding == ForInBinding::kPerIterationLet) Append("let ");
    Name(node.key); Append(" in ");
    Push(Statement{node.body, at.depth}); Push(std::string_view(") "));
    Push(ExpressionOperand{node.object, {ExpressionGrammar::kExpression}});
  }
  void StatementNode(ForOfStmt node, Statement at) {
    if (node.binding != ForOfBinding::kAssignment) throw Error::kUnsupportedContext;
    if (node.label) { Label(*node.label); Append(": "); }
    Append("for (");
    // ECMA-262 ForInOfStatement forbids the unparenthesized identifier async
    // as the LHS of synchronous for-of. This is a spelling/grammar boundary.
    const auto target = Initializers().Storage(node.target);
    const bool grouped = Tree().names[Tree().symbols[target.value].name.value] == u"async";
    if (grouped) Append("(");
    Name(node.target);
    if (grouped) Append(")");
    Append(" of ");
    Push(Statement{node.body, at.depth}); Push(std::string_view(") "));
    Push(ExpressionOperand{node.iterable, {ExpressionGrammar::kAssignmentValue}});
  }
  void StatementNode(LoopStmt node, Statement at) {
    if (const auto& syntax = Loops().conditions[at.id.value]) {
      const auto& transition = Loops().transitions[at.id.value];
      const auto& initializer = Loops().initializers[at.id.value];
      const bool native_for = transition || initializer;
      const auto declarations = Initializers().loop_declarations[at.id.value];
      if (!declarations) {
        if (node.label) { Label(*node.label); Append(": "); }
        Append(native_for ? "for (" : "while (");
      }
      Push(LoopBody{node.body, *syntax, at.depth}); Push(std::string_view(") "));
      if (native_for) {
        if (transition) {
          Push(LoopTransitionEnd{transition->block, transition->end});
          Push(LoopHeaderCursor{transition->block, transition->first, transition->end, {}, true});
        }
        Push(std::string_view("; "));
      }
      Push(LoopHeaderCursor{node.body, 0, syntax->header_count, syntax->body});
      if (native_for) {
        Push(std::string_view("; "));
        if (initializer) {
          if (declarations) {
            Push(LoopDeclarationCursor{*declarations, initializer->end}); Push(std::string_view("let "));
            Push(LoopKeyword{node.label});
            if (Initializers().entry_counts[initializer->first] != Initializers().entry_counts[*declarations]) {
              // Prefix effects execute once, before the new lexical loop scope.
              // Keeping them outside the RHS also preserves NamedEvaluation.
              Push(Indent{at.depth}); Push(std::string_view(");\n"));
              Push(LoopInitializerCursor{initializer->first, *declarations}); Append("(");
            }
          } else Push(LoopInitializerCursor{initializer->first, initializer->end});
        }
      }
    } else {
      if (node.label) { Label(*node.label); Append(": "); }
      Append("while (true) "); Push(Statement{node.body, at.depth});
    }
  }
  const LoopSyntax& Loops() const { return Variables().loops; }
  void Run(ElideStatement at) {
    // The prepared plan owns omission. Traverse all owned source origins once
    // at the current boundary; emission performs no new semantic decisions.
    OmitBindingStatement(at.id);
    const auto& node = Tree().statements[at.id.value].statement;
    if (const auto* block = std::get_if<BlockStmt>(&node))
      for (std::uint32_t i = 0; i <= block->child_count; ++i) { Step(); Boundary({at.id, i}); }
    StatementChildren(Tree(), at.id, [&](StmtId child) { Step(); Push(ElideStatement{child}); });
  }
  void Run(LoopHeader at) {
    if (at.retained == at.id) { Append("true"); return; }
    Result().statements[at.id.value].begin = written_; Push(EndStatement{at.id});
    const auto& node = Tree().statements[at.id.value].statement;
    if(const auto value=Initializers().statement_values[at.id.value]) {
      StatementChildren(Tree(),at.id,[&](StmtId child){Push(ElideStatement{child});});
      ProjectDeclarationOrigins(at.id,value);Append("(");Push(std::string_view(at.discarded?")":", true)"));
      Push(ExpressionOperand{*value,{}, {},ExpressionObservation::kDiscarded});return;
    }
    if(const auto& completion=Initializers().declaration_completions[at.id.value]) {
      PrintCompletion(*completion,at.id,false,true);return;
    }
    if(const auto& value=Initializers().declaration_values[at.id.value]) {
      ProjectDeclarationOrigins(at.id,value->value);
      Append("(");Push(std::string_view(at.discarded ? ")" : ", true)"));
      if(value->target) {
        Name(*value->target);Append(" = ");
        Push(ExpressionOperand{value->value,{ExpressionGrammar::kAssignmentValue},
            Tree().symbols[Initializers().Storage(*value->target).value].name});
      }else Push(ExpressionOperand{value->value,{ExpressionGrammar::kAssignmentValue}, {},ExpressionObservation::kDiscarded});
      return;
    }
    if (const auto* effect = std::get_if<ExpressionStmt>(&node)) {
      Append("("); Push(std::string_view(at.discarded ? ")" : ", true)"));
      Push(ExpressionOperand{effect->expression, {ExpressionGrammar::kAssignmentValue}, {}, ExpressionObservation::kDiscarded});
    } else if (std::holds_alternative<BreakStmt>(node) || std::holds_alternative<ContinueStmt>(node)) Append("false");
    else if (const auto* block = std::get_if<BlockStmt>(&node)) {
      Append("("); Push(std::string_view(")"));
      Push(LoopHeaderCursor{at.id, 0, block->child_count, at.retained, at.discarded});
    } else {
      const auto& branch = std::get<IfStmt>(node);
      const auto literal = [&](std::optional<StmtId> arm) -> std::optional<bool> {
        if (!arm || arm == at.retained) return true;
        return Loops().meanings[arm->value].literal;
      };
      const auto yes = literal(branch.consequent), no = literal(branch.alternate);
      if (yes && no) {
        if (branch.consequent != at.retained) Push(ElideStatement{branch.consequent});
        if (branch.alternate && branch.alternate != at.retained) Push(ElideStatement{*branch.alternate});
        if (*yes != *no) {
          Append("("); Push(std::string_view(")"));
          Push(BooleanOperand{{branch.condition, {ExpressionGrammar::kExpression}, {}, ExpressionObservation::kTruthiness}, !*yes});
        }
        else {
          Append("("); Push(std::string_view(*yes ? ", true)" : ", false)"));
          Push(ExpressionOperand{branch.condition, {ExpressionGrammar::kAssignmentValue}, {}, ExpressionObservation::kDiscarded});
        }
      } else {
        Append("("); Push(std::string_view(")"));
        if (branch.alternate) Push(LoopHeader{*branch.alternate, at.retained});
        else Push(std::string_view("true"));
        Push(std::string_view(" : ")); Push(LoopHeader{branch.consequent, at.retained});
        Push(std::string_view(") ? ")); Append("(");
        Push(ExpressionOperand{branch.condition, {ExpressionGrammar::kExpression}, {}, ExpressionObservation::kTruthiness});
      }
    }
  }
  void Run(LoopHeaderCursor at) {
    Boundary({at.block, at.index});
    const auto& block = std::get<BlockStmt>(Tree().statements[at.block.value].statement);
    const auto& counts = Initializers().child_counts;
    if (at.index == at.count) {
      if (counts[block.first_child] == counts[block.first_child + at.count]) Append("true");
      return;
    }
    const auto child = Tree().children[block.first_child + at.index];
    if (Initializers().omitted[child.value]) {
      Push(LoopHeaderCursor{at.block, at.index + 1, at.count, at.retained, at.discarded});
      Push(ElideStatement{child});return;
    }
    const auto& meaning = Loops().meanings[child.value];
    const bool follows = counts[block.first_child + at.index + 1] != counts[block.first_child + at.count];
    if (follows) {
      if (meaning.exit) Push(std::string_view(")"));
      Push(LoopHeaderCursor{at.block, at.index + 1, at.count, at.retained, at.discarded});
      Push(std::string_view(meaning.exit ? " && (" : ", "));
    } else {
      // Walk remaining elided roots as well: they emit no separator, but their
      // source identities still need omission events at the predicate boundary.
      Push(LoopHeaderCursor{at.block, at.index + 1, at.count, at.retained, at.discarded});
    }
    if (const auto* effect = std::get_if<ExpressionStmt>(&Tree().statements[child.value].statement); effect && follows) {
      Result().statements[child.value].begin = written_; Push(EndStatement{child});
      Push(ExpressionOperand{effect->expression, {ExpressionGrammar::kAssignmentValue}, {}, ExpressionObservation::kDiscarded});
    } else Push(LoopHeader{child, at.retained, at.discarded && !follows});
  }
  void Run(LoopTransitionEnd at) {
    const auto& block = std::get<BlockStmt>(Tree().statements[at.block.value].statement);
    for (auto i = at.first; i <= block.child_count; ++i) {
      Step(); Boundary({at.block, i});
      if (i < block.child_count) Push(ElideStatement{Tree().children[block.first_child + i]});
    }
  }
  void Run(BlockBoundary at) { Boundary(at); }
  void OmitBindingStatement(StmtId id) {
    Result().statements[id.value] = {written_,written_};
    Result().statement_extents[id.value] = GeneratedExtent::kBoundary;
    VisitStatementExpressions(Tree().statements[id.value].statement,[&](ExprId expression) {
      // Mark every omitted owned occurrence, excluding exactly the moved
      // RHS. Skipping the whole original tree would lose wrapper origins;
      // projecting the RHS would overwrite its retained destination span.
      if(Initializers().forwarded_values[id.value]==expression)return;
      Result().expressions[expression.value] = {written_,written_};
      Result().expression_extents[expression.value] = GeneratedExtent::kBoundary;
      Push(ProjectedExpression{expression,written_,written_,GeneratedExtent::kBoundary,
          Initializers().forwarded_values[id.value]});
    });
  }
  void Run(LoopInitializerCursor at) {
    if (at.index == at.end) return;
    const auto& root = Loops().initializer_roots[at.index];
    Boundary(root.boundary);
    const BlockBoundary after{root.boundary.block, root.boundary.index + 1};
    const bool omitted = Initializers().omitted[root.statement.value];
    Push(LoopInitializerCursor{at.index + 1,at.end,at.emitted || !omitted});Push(after);
    if (omitted) { Push(ElideStatement{root.statement});return; }
    if (at.emitted) Append(", ");
    Push(LoopHeader{root.statement, {}, true});
  }
  void Run(LoopKeyword at) {
    if (at.label) { Label(*at.label); Append(": "); }
    Append("for (");
  }
  void Run(LoopDeclarationCursor at) {
    if (at.index == at.end) return;
    const auto& root = Loops().initializer_roots[at.index]; Boundary(root.boundary);
    const BlockBoundary after{root.boundary.block, root.boundary.index + 1};
    const bool omitted = Initializers().omitted[root.statement.value];
    Push(LoopDeclarationCursor{at.index + 1,at.end,at.emitted || !omitted});Push(after);
    if (omitted) { Push(ElideStatement{root.statement});return; }
    if (at.emitted) Append(", ");
    Result().statements[root.statement.value].begin = written_;
    const auto& binding = *Initializers().statements[root.statement.value];
    if(const auto& completion=Initializers().declaration_completions[root.statement.value]) {
      Push(EndStatement{root.statement});PrintCompletion(*completion,root.statement,true);return;
    }
    const auto& assignment = std::get<AssignmentExpr>(Tree().expressions[binding.assignment.value].expression);
    Push(EndStatement{root.statement});
    BeginDeclarationExpression(std::get<ExpressionStmt>(Tree().statements[root.statement.value].statement).expression,
        binding.assignment);
    Name(binding.symbol); Append(" = ");
    Push(ExpressionOperand{assignment.value, {ExpressionGrammar::kAssignmentValue}, Tree().symbols[Initializers().Storage(binding.symbol).value].name});
  }
  void Run(LoopBody at) {
    Result().statements[at.block.value].begin = written_; Push(EndStatement{at.block});
    Append("{\n"); Push(BlockCursor{at.block, at.syntax.header_count, at.depth});
    if (at.syntax.body) {
      Push(std::string_view("\n")); Push(Statement{*at.syntax.body, at.depth + 1}); Push(Indent{at.depth + 1});
    }
  }
  void Label(LabelId label) {
    const auto target = Tree().labels[label.value];
    Append(std::holds_alternative<BlockStmt>(Tree().statements[target.value].statement) ? "$exit" : "$loop");
    Numeric(label.value);
  }
  void Transfer(std::string_view token, std::optional<LabelId> label) {
    Append(token);
    if (label) { Append(" "); Label(*label); }
    Append(";");
  }
  void StatementNode(BreakStmt node, Statement at) { Transfer("break", Variables().readable.plain_breaks[at.id.value] ? std::nullopt : node.label); }
  void StatementNode(ContinueStmt node, Statement) { Transfer("continue", node.label); }
  void StatementNode(DebuggerStmt, Statement) { Append("debugger;"); }
  void StatementNode(UnrecoveredStmt node, Statement) {
    const auto& issue = Tree().recovery_issues[node.issue.value];
    Append("/* UNRECOVERED: ");
    switch (issue.reason) {
      case RecoveryReason::kUnsupportedOperation: Append("unsupported operation"); break;
      case RecoveryReason::kUnsupportedControl: Append("unsupported control flow"); break;
      case RecoveryReason::kUnavailableValue: Append("unavailable value"); break;
      case RecoveryReason::kUnresolvedBinding: Append("unresolved binding"); break;
      case RecoveryReason::kIncompleteChild: Append("incomplete child function"); break;
    }
    if (issue.begin) {
      Append("; PC "); Numeric(issue.begin->value);
      if (issue.end) { Append(".."); Numeric(issue.end->value); }
    }
    Append(" */");
  }
  void Run(Statement at) {
    depth_ = at.depth;
    Result().statements[at.id.value].begin = written_;
    Push(EndStatement{at.id});
    if(const auto value=Initializers().statement_values[at.id.value]) {
      StatementChildren(Tree(),at.id,[&](StmtId child){Push(ElideStatement{child});});
      ProjectDeclarationOrigins(at.id,value);Push(std::string_view(";"));
      Push(ExpressionOperand{*value,{}, {},ExpressionObservation::kDiscarded});
    }else if(const auto& completion=Initializers().declaration_completions[at.id.value])PrintCompletion(*completion,at.id);
    else if(const auto& value=Initializers().declaration_values[at.id.value])PrintDeclarationValue(*value,at.id);
    else std::visit([&](const auto& node) { StatementNode(node, at); }, Tree().statements[at.id.value].statement);
  }
  void Run(EndStatement end) { Result().statements[end.id.value].end = written_; }
  void Run(BlockCursor at) {
    const auto& block = std::get<BlockStmt>(Tree().statements[at.id.value].statement);
    const auto tail = Loops().tail_starts[at.id.value];
    const auto end = tail.value_or(block.child_count);
    const auto moved = [&](std::uint32_t index) {
      return Loops().initializer_owners[Tree().children[block.first_child + index].value].has_value();
    };
    const bool relocated = at.index < end && moved(at.index);
    if ((!tail || at.index < end) && !relocated && !(at.index && moved(at.index - 1))) Boundary({at.id, at.index});
    const bool root = (ModuleBody() && at.id == Tree().body) || at.form!=BlockForm::kBraced;
    if (at.index == end) { if (!root) { Run(Indent{at.depth}); Append("}"); } return; }
    Push(BlockCursor{at.id, at.index + 1, at.depth, at.form});
    if (relocated) return;
    if (at.id == Tree().body && at.index + 1 == block.child_count) {
      if (const auto completion = ImplicitTailCompletion(Tree())) {
        // Keep the actual tail event at this byte boundary. Modules end here;
        // native callables still emit their closing block/function syntax.
        // Retain the original return and literal/receiver operand identities.
        Result().statements[completion->statement.value] = {written_, written_};
        Result().statement_extents[completion->statement.value] = GeneratedExtent::kBoundary;
        if (completion->value) {
          Result().expressions[completion->value->value] = {written_, written_};
          Result().expression_extents[completion->value->value] = GeneratedExtent::kBoundary;
        }
        return;
      }
    }
    const auto child = Tree().children[block.first_child + at.index];
    if (const auto read = DiscardedInvocationRead(Tree(), child)) {
      Result().statements[child.value] = {written_, written_};
      Result().statement_extents[child.value] = GeneratedExtent::kBoundary;
      Result().expressions[read->value] = {written_, written_};
      Result().expression_extents[read->value] = GeneratedExtent::kBoundary;
      return;
    }
    if ((!Variables().object_rest_fields.empty()&&Variables().object_rest_fields[child.value]) || Initializers().omitted[child.value] || Variables().readable.omit_snapshots[child.value] || Variables().readable.guards[child.value]==ReadableGuard::kRemove) {
      Push(ElideStatement{child}); return;
    }
    if(at.form==BlockForm::kElseIf){Push(Statement{child,at.depth});return;}
    const auto depth = root ? at.depth : at.depth + 1;
    if(Variables().braces.statements[child.value]==BraceReason::kStatementList){
      Push(Statement{child,depth,BlockForm::kList});return;
    }
    Push(std::string_view("\n"));
    Push(Statement{Tree().children[block.first_child + at.index], depth});
    Push(Indent{depth});
  }
  void PrepareAnchors() {
    if(enclosing_names_) {
      // This synchronous borrow is scoped by exact tree object identity. The
      // outer name analysis owns the immutable plans until emission finishes;
      // destination/global/capture checks still run independently above.
      const auto found=enclosing_names_->Lookup(Tree());Step(found.work);
      if(!found.plan||found.plan->original!=Original())throw Error::kUnsupportedContext;
      if(borrowed_variables_.empty())Array(borrowed_variables_,family_->members.size());
      borrowed_variables_[current_]=found.plan;
    }else {
      if (family_ && family_variables_.empty()) Array(family_variables_, family_->members.size());
      Site(TreePrintSite::kVariableProjection);
      auto planned = PrepareSourceVariables(Tree(), remaining_,Original());
      if (const auto* error = std::get_if<TreeStructureError>(&planned)) {
        if (*error == TreeStructureError::kResourceLimit) throw Error::kResourceLimit;
        if (*error == TreeStructureError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kUnsupportedContext;
      }
      auto& variables = family_ ? family_variables_[current_] : variables_;
      variables = std::get<SourceVariables>(std::move(planned));
      Admit(variables.allocated_bytes, 1); Step(variables.work);
      if(variables.retained_bytes>variables.allocated_bytes)throw Error::kUnsupportedContext;
      remaining_.bytes+=variables.allocated_bytes-variables.retained_bytes;
    }
    Site(TreePrintSite::kPositionTable);
    Array(Located(), Tree().anchors.size());
    std::size_t count = 0;
    for (std::size_t i = 0; i < Tree().anchors.size(); ++i) {
      Step();
      if (const auto* position = std::get_if<BlockBoundary>(&Tree().anchors[i].position))
        Located()[count++] = {*position, {static_cast<std::uint32_t>(i)}};
    }
    Located().resize(count);
    std::sort(Located().begin(), Located().end(), [&](const auto& left, const auto& right) {
      Step(); return Less(left.boundary, right.boundary);
    });
  }
  static bool Less(BlockBoundary left, BlockBoundary right) {
    return left.block != right.block ? left.block < right.block : left.index < right.index;
  }
  const SourceVariables& Variables() const {
    if(enclosing_names_)return *borrowed_variables_[current_];
    return family_ ? family_variables_[current_] : variables_;
  }
  const NullishSyntax& Nullish() const { return Variables().nullish; }
  const BindingInitializers& Initializers() const { return Variables().bindings; }
  void FinalizeNullishRanges() {
    const auto& syntax = Nullish();
    const auto finalize = [&](const auto& omitted, auto& ranges, auto& extents) {
      for (std::size_t i = 0; i < omitted.size(); ++i) {
        Step(); if (!omitted[i]) continue;
        const auto base = syntax.Base(Tree(), *omitted[i]);
        const auto boundary = Result().expressions[base.value].end;
        ranges[i] = {boundary, boundary};
        extents[i] = GeneratedExtent::kBoundary;
      }
    };
    finalize(syntax.omitted_by, Result().expressions, Result().expression_extents);
    finalize(syntax.omitted_properties_by, Result().properties, Result().property_extents);
    const auto statements=[&](const auto& omitted,auto& ranges,auto& extents) {
      for(std::size_t i=0;i<omitted.size();++i) {
        Step();if(!omitted[i]) continue;
        const auto base=syntax.Base(Tree(),NullishRoot{*omitted[i]});
        const auto boundary=Result().expressions[base.value].end;ranges[i]={boundary,boundary};extents[i]=GeneratedExtent::kBoundary;
      }
    };
    statements(syntax.statement_expressions_by,Result().expressions,Result().expression_extents);
    statements(syntax.statement_properties_by,Result().properties,Result().property_extents);
    statements(syntax.statement_omitted_by,Result().statements,Result().statement_extents);
    if(!syntax.statement_omitted_by.empty()) for(std::size_t i=0;i<Tree().anchors.size();++i) {
      Step();if(const auto* boundary=std::get_if<BlockBoundary>(&Tree().anchors[i].position))
        if(const auto root=syntax.statement_omitted_by[boundary->block.value])
          Result().anchors[i]=Result().expressions[syntax.Base(Tree(),NullishRoot{*root}).value].end;
    }
  }
  void Boundary(BlockBoundary position) {
    // Binary lookup per structural boundary, not a scan of every method anchor.
    auto found = std::lower_bound(Located().begin(), Located().end(), position,
        [&](const auto& item, auto key) { Step(); return Less(item.boundary, key); });
    while (found != Located().end() && !Less(position, found->boundary)) {
      if(auto* trace=TreePrintTrace::Active()){trace->failure.anchor=found->id;Origin(Tree().anchors[found->id.value].origin);}
      Step(); Result().anchors[found->id.value] = written_; ++found;
    }
  }
  const VerifiedFunction* function_ = nullptr;
  const FunctionTree* tree_ = nullptr;
  const OriginalFlow* original_ = nullptr;
  const ConstructedFamily* family_ = nullptr;
  const ClassTreeContext* class_root_ = nullptr;
  std::ostream& output_;
  std::size_t limit_;
  analysis::SemanticBudget initial_;
  analysis::SemanticBudget remaining_;
  std::size_t peak_bytes_ = 0;
  const GlobalScope* scope_;
  bool has_external_binding_ = false;
  PrintedFunction result_;
  PrintedFamilyDraft draft_;
  std::size_t current_ = 0;
  std::size_t depth_ = 0;
  std::size_t written_ = 0;
  std::vector<OwnedAction> actions_;
  bool module_body_ = false;
  std::optional<ModuleDefaultExport> default_export_;
  bool static_fields_ = false;
  bool static_block_ = false;
  std::vector<LocatedAnchor> located_;
  std::vector<std::vector<LocatedAnchor>> locations_;
  std::vector<OwnerTree> owners_;
  std::vector<std::uint8_t> printed_;
  std::vector<std::uint8_t> native_names_;
  ClassNames class_names_;
  const ClassNames* enclosing_names_=nullptr;
  std::vector<const SourceVariables*> borrowed_variables_;
  SourceVariables variables_;
  std::vector<SourceVariables> family_variables_;
};
}  // namespace

std::variant<PrintedFunction, TreePrintFailure> PrintStaticFields(
    const FunctionTree& tree, std::ostream& output, EmitBudget limit, analysis::SemanticBudget budget, std::size_t indent,
    const GlobalScope* scope, std::optional<abc::EntityOffset> record,const OriginalFlow* original) {
  TreePrintTrace trace;
  TreePrintLocationScope location(tree);
  try {
    if (!tree.modules.empty() && !record) return Error::kUnsupportedContext;
    TreePrintSiteScope site(TreePrintSite::kDestinationVerification);
    const auto destination = VerifyModuleDestination(tree, record.value_or(abc::EntityOffset{}), scope, budget);
    const auto* verified = std::get_if<ModuleDestination>(&destination);
    if (!verified) {
      const auto error = std::get<ModuleEnclosureError>(destination);
      if (error == ModuleEnclosureError::kResourceLimit) return trace.Capture(Error::kResourceLimit);
      if (error == ModuleEnclosureError::kWorkLimit) return trace.Capture(Error::kWorkLimit);
      return Error::kUnsupportedContext;
    }
    budget.work -= verified->work;
    auto result = TreePrinter(tree, output, limit, budget, scope,original).Direct(false, true, indent);
    result.work += verified->work;
    if (!tree.modules.empty()) result.module_binding = PrintedFunction::ModuleBinding{*record, scope->module_enclosure->Owner()};
    return result;
  } catch (const TreePrintFailure& failure) { return failure;
  } catch (Error error) { return trace.Capture(error);
  } catch (const std::bad_alloc&) { PrintCharge(TreePrintBudgetKind::kHostAllocation,{},0,0); return trace.Capture(Error::kResourceLimit);
  } catch (const std::length_error&) { PrintCharge(TreePrintBudgetKind::kCapacityOverflow,{},0,0); return trace.Capture(Error::kResourceLimit);
  } catch (const std::ios_base::failure&) { return Error::kWrite; }
}

std::variant<PrintedFunction, TreePrintFailure> PrintFunctionTree(
    const FunctionTree& tree, std::ostream& output,
    EmitBudget limit, analysis::SemanticBudget scratch, const GlobalScope* scope,const OriginalFlow* original) {
  TreePrintTrace trace;
  try { return TreePrinter(tree, output, limit, scratch, scope,original).Direct();
  } catch (const TreePrintFailure& failure) { return failure;
  } catch (Error error) { return trace.Capture(error);
  } catch (const std::bad_alloc&) { PrintCharge(TreePrintBudgetKind::kHostAllocation,{},0,0); return trace.Capture(Error::kResourceLimit);
  } catch (const std::length_error&) { PrintCharge(TreePrintBudgetKind::kCapacityOverflow,{},0,0); return trace.Capture(Error::kResourceLimit);
  } catch (const std::ios_base::failure&) { return Error::kWrite; }
}

std::variant<PrintedFunction, TreePrintFailure> PrintModuleTree(
    const FunctionTree& tree, std::ostream& output,
    EmitBudget limit, analysis::SemanticBudget scratch, const GlobalScope* scope, std::optional<ModuleDefaultExport> default_export,const OriginalFlow* original) {
  TreePrintTrace trace;
  try { return TreePrinter(tree, output, limit, scratch, scope,original).Direct(true, false, 0, default_export);
  } catch (const TreePrintFailure& failure) { return failure;
  } catch (Error error) { return trace.Capture(error);
  } catch (const std::bad_alloc&) { PrintCharge(TreePrintBudgetKind::kHostAllocation,{},0,0); return trace.Capture(Error::kResourceLimit);
  } catch (const std::length_error&) { PrintCharge(TreePrintBudgetKind::kCapacityOverflow,{},0,0); return trace.Capture(Error::kResourceLimit);
  } catch (const std::ios_base::failure&) { return Error::kWrite; }
}

std::variant<PrintedFamilyDraft, TreePrintFailure> PrintFamilyDraft(const ConstructedFamily& family,
    std::ostream& output, EmitBudget limit, analysis::SemanticBudget scratch, const GlobalScope* scope) {
  TreePrintTrace trace;
  try { return TreePrinter(family, output, limit, scratch, nullptr, scope).Draft();
  } catch (const TreePrintFailure& failure) { return failure;
  } catch (Error error) { return trace.Capture(error);
  } catch (const std::bad_alloc&) { PrintCharge(TreePrintBudgetKind::kHostAllocation,{},0,0); return trace.Capture(Error::kResourceLimit);
  } catch (const std::length_error&) { PrintCharge(TreePrintBudgetKind::kCapacityOverflow,{},0,0); return trace.Capture(Error::kResourceLimit);
  } catch (const std::ios_base::failure&) { return Error::kWrite; }
}
std::variant<PrintedFamilyDraft, TreePrintFailure> PrintModuleFamilyDraft(const ConstructedFamily& family,
    std::ostream& output, EmitBudget limit, analysis::SemanticBudget scratch, const GlobalScope* scope, std::optional<ModuleDefaultExport> default_export) {
  TreePrintTrace trace;
  try { return TreePrinter(family, output, limit, scratch, nullptr, scope).Draft(0, true, default_export);
  } catch (const TreePrintFailure& failure) { return failure;
  } catch (Error error) { return trace.Capture(error);
  } catch (const std::bad_alloc&) { PrintCharge(TreePrintBudgetKind::kHostAllocation,{},0,0); return trace.Capture(Error::kResourceLimit);
  } catch (const std::length_error&) { PrintCharge(TreePrintBudgetKind::kCapacityOverflow,{},0,0); return trace.Capture(Error::kResourceLimit);
  } catch (const std::ios_base::failure&) { return Error::kWrite; }
}
std::variant<PrintedFamilyDraft, TreePrintFailure> PrintClassFamilyDraft(const ConstructedFamily& family,
    const ClassTreeContext& context, std::ostream& output, EmitBudget limit, analysis::SemanticBudget scratch, const GlobalScope* scope, std::size_t indent,
    const ClassNames* prepared) {
  TreePrintTrace trace;
  try { return TreePrinter(family, output, limit, scratch, &context, scope, prepared).Draft(indent);
  } catch (const TreePrintFailure& failure) { return failure;
  } catch (Error error) { return trace.Capture(error);
  } catch (const std::bad_alloc&) { PrintCharge(TreePrintBudgetKind::kHostAllocation,{},0,0); return trace.Capture(Error::kResourceLimit);
  } catch (const std::length_error&) { PrintCharge(TreePrintBudgetKind::kCapacityOverflow,{},0,0); return trace.Capture(Error::kResourceLimit);
  } catch (const std::ios_base::failure&) { return Error::kWrite; }
}

std::variant<PrintedFunction, TreePrintFailure> PrintFunctionTree(
    const VerifiedFunction& function, std::ostream& output,
    EmitBudget output_budget, analysis::SemanticBudget scratch, const GlobalScope* scope) {
  TreePrintTrace trace;
  TreePrintLocationScope location(function.Tree());
  try {
    if (!function.Receipt().record) return Error::kUnsupportedContext;
    TreePrintSiteScope site(TreePrintSite::kDestinationVerification);
    const auto destination = VerifyModuleDestination(function.Tree(), *function.Receipt().record, scope, scratch);
    const auto* modules = std::get_if<ModuleDestination>(&destination);
    if (!modules) {
      const auto error = std::get<ModuleEnclosureError>(destination);
      if (error == ModuleEnclosureError::kResourceLimit) return trace.Capture(Error::kResourceLimit);
      if (error == ModuleEnclosureError::kWorkLimit) return trace.Capture(Error::kWorkLimit);
      return Error::kUnsupportedContext;
    }
    scratch.work -= modules->work;
    const auto globals = VerifyTreeGlobals(function.Tree(), scope, scratch);
    const auto* checked = std::get_if<TreeGlobals>(&globals);
    if (!checked) {
      const auto error = std::get<TreeGlobalError>(globals);
      if (error == TreeGlobalError::kResourceLimit) return trace.Capture(Error::kResourceLimit);
      if (error == TreeGlobalError::kWorkLimit) return trace.Capture(Error::kWorkLimit);
      return Error::kUnsupportedContext;
    }
    scratch.work -= checked->work;
    auto result = TreePrinter(function, output, output_budget, scratch, scope).Print();
    result.work += checked->work + modules->work;
    result.allocated_bytes = std::max(result.allocated_bytes, checked->allocated_bytes);
    return result;
  } catch (const TreePrintFailure& failure) { return failure;
  } catch (Error error) { return trace.Capture(error);
  } catch (const std::bad_alloc&) { PrintCharge(TreePrintBudgetKind::kHostAllocation,{},0,0); return trace.Capture(Error::kResourceLimit);
  } catch (const std::length_error&) { PrintCharge(TreePrintBudgetKind::kCapacityOverflow,{},0,0); return trace.Capture(Error::kResourceLimit);
  } catch (const std::ios_base::failure&) { return Error::kWrite; }
}
}  // namespace abcd::source

namespace abcd::source {
namespace {
std::variant<PrintedClassTree, TreePrintFailure, FamilyPrepareFailure> PrintClassPart(
    const ClassTree& tree, std::ostream& output, EmitBudget limit,
    analysis::SemanticBudget budget, const GlobalScope* globals, std::size_t indent, ClassPrintForm form,
    const ClassCaptureDestination* parent, const ClassNames* prepared) {
  using Failure = TreePrintError;
  const auto initial = budget;
  TreePrintTrace trace;
  trace.failure.site=TreePrintSite::kClassMemberRetention;
  trace.failure.location.method=tree.creation.target;
  try {
    const auto readiness_work = tree.members.size() + tree.omissions.size() + tree.definitions.size();
    if (readiness_work > budget.work) {PrintCharge(TreePrintBudgetKind::kWork,readiness_work,budget.work,initial.work);return trace.Capture(Failure::kWorkLimit);}
    budget.work -= readiness_work;
    if (!tree.DraftPrepared() || ResourceFailures(tree.identity.contract) != ResourceFailureScope::kExcluded ||
        tree.constructor->Identity() != tree.identity ||
        tree.constructor->Family().members[tree.constructor->Family().root.value].tree.method != tree.creation.target)
      return Failure::kUnsupportedContext;
    // Only the enclosing ClassExpr may emit a derived header/heritage. The
    // public whole-class route remains restricted to actual base constructors.
    if (tree.constructor->Family().members[tree.constructor->Family().root.value].tree.callable != tree.callable ||
        tree.callable !=
        (form == ClassPrintForm::kDerivedBody ? CallableKind::kDerivedConstructor : CallableKind::kBaseConstructor))
      return Failure::kUnsupportedContext;
    if (tree.class_self && tree.class_self->certificate &&
        (form == ClassPrintForm::kExpression ||
         form == ClassPrintForm::kInferredExpression))
      return Failure::kUnsupportedContext;
    if (!parent) {
      if (tree.initializer && !tree.initializer->tree.captures.empty())
        return Failure::kUnsupportedContext;
      if (tree.constructor->Family().root_capture) return Failure::kUnsupportedContext;
      for (const auto& member : tree.members) if (member && member->Family().root_capture) return Failure::kUnsupportedContext;
      for (const auto& site : tree.definitions)
        for (const auto& member : site.bodies)
          if (member && member->Family().root_capture) return Failure::kUnsupportedContext;
    }
    PrintedClassTree result;
    bool member_gaps = !tree.omissions.empty();
    auto count = tree.members.size() + 1;
    for (const auto& accessor : tree.definitions) {
      if (!budget.work) {PrintCharge(TreePrintBudgetKind::kWork,1,budget.work,initial.work);return trace.Capture(Failure::kWorkLimit);}
      --budget.work; count += accessor.bodies[0].has_value() + accessor.bodies[1].has_value();
      member_gaps |= accessor.omissions[0].has_value() || accessor.omissions[1].has_value();
    }
    if (count > budget.bytes / sizeof(PrintedClassTree::Member)) {PrintByteCharge(count,sizeof(PrintedClassTree::Member),budget.bytes,initial.bytes,initial.bytes-budget.bytes);return trace.Capture(Failure::kResourceLimit);}
    budget.bytes -= count * sizeof(PrintedClassTree::Member); result.members.reserve(count);
    const auto excess = result.members.capacity() - count;
    if (excess > budget.bytes / sizeof(PrintedClassTree::Member)) {PrintByteCharge(excess,sizeof(PrintedClassTree::Member),budget.bytes,initial.bytes,initial.bytes-budget.bytes);return trace.Capture(Failure::kResourceLimit);}
    budget.bytes -= excess * sizeof(PrintedClassTree::Member);
    const auto append = [&](std::string_view text) {
      TreePrintSiteScope site(TreePrintSite::kTokenOutput);
      trace.failure.written_bytes=result.bytes;
      if (text.size() > limit.bytes - result.bytes) {PrintCharge(TreePrintBudgetKind::kOutputBytes,text.size(),limit.bytes-result.bytes,limit.bytes,result.bytes);throw Failure::kOutputLimit;}
      if (text.size() > budget.work) {PrintCharge(TreePrintBudgetKind::kWork,text.size(),budget.work,initial.work);throw trace.Capture(Failure::kWorkLimit);}
      budget.work -= text.size(); output << text;
      if (!output) throw Failure::kWrite;
      result.bytes += text.size();
    };
    const auto name = [&] {
      if (tree.name.size() > budget.work) {PrintCharge(TreePrintBudgetKind::kWork,tree.name.size(),budget.work,initial.work);throw trace.Capture(Failure::kWorkLimit);}
      budget.work -= tree.name.size();
      const auto printed = WriteStringLiteral(tree.name, output, {limit.bytes - result.bytes});
      if (const auto* bytes = std::get_if<std::size_t>(&printed)) result.bytes += *bytes;
      else {
        switch (std::get<EmitError>(printed)) {
          case EmitError::kOutputLimit: throw Failure::kOutputLimit;
          case EmitError::kWrite: throw Failure::kWrite;
          default: throw Failure::kInvalidLiteral;
        }
      }
    };
    const auto number = [&](std::size_t value) {
      std::array<char, std::numeric_limits<std::size_t>::digits10 + 1> buffer;
      const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
      if (converted.ec != std::errc{}) throw Failure::kUnsupportedContext;
      append(std::string_view(buffer.data(), static_cast<std::size_t>(converted.ptr - buffer.data())));
    };
    const FamilyDestination destination{Strictness::kStrict, globals, FunctionFragment::kClassMember, {}, nullptr, parent};
    const auto member = [&](const PreparedFamily& family) {
      auto printed = family.PrintDraft(destination, output, {limit.bytes - result.bytes}, budget, indent + 1, prepared);
      if (const auto* failure = std::get_if<FamilyPrepareFailure>(&printed)) throw *failure;
      if (const auto* failure = std::get_if<TreePrintFailure>(&printed)) throw *failure;
      auto positions = std::get<PrintedFamilyDraft>(std::move(printed));
      for (const auto& body : positions.members) {result.incomplete |= body.incomplete;result.arkts |= body.arkts;}
      if (positions.allocated_bytes > budget.bytes) {PrintByteCharge(positions.allocated_bytes,1,budget.bytes,initial.bytes,initial.bytes-budget.bytes);throw Failure::kResourceLimit;}
      if (positions.work > budget.work) {PrintCharge(TreePrintBudgetKind::kWork,positions.work,budget.work,initial.work);throw trace.Capture(Failure::kWorkLimit);}
      budget.bytes -= positions.allocated_bytes; budget.work -= positions.work;
      const auto offset = result.bytes; result.bytes += positions.bytes;
      result.members.push_back({offset, std::move(positions)});
    };
    // Computed-property NamedEvaluation preserves runtime class.name without
    // inventing an immutable class-name binding that shadows a mutable module
    // export read inside a method. The wrapper is never exposed to user code.
    if (member_gaps) append("/* INCOMPLETE class: unrecovered members */\n");
    if (form == ClassPrintForm::kExpression) { append("({["); name(); append("]: class {\n"); }
    else if (form == ClassPrintForm::kNamedExpression || form == ClassPrintForm::kBareNamedExpression || form == ClassPrintForm::kDeclaration) {
      append(form == ClassPrintForm::kNamedExpression ? "(class " : "class ");
      for (const auto unit : tree.name) {
        const char byte = static_cast<char>(unit); append(std::string_view(&byte,1));
      }
      append(" {\n");
    }
    else if (form == ClassPrintForm::kInferredExpression) append("class {\n");
    else append(" {\n");
    std::uint64_t sendable_work=0;
    if(const auto failure=VerifySendableClass(tree,true,budget,&sendable_work)) {
      if(failure->kind==SendableError::kWorkLimit)throw Failure::kWorkLimit;
      throw FamilyPrepareFailure{FamilyPrepareError::kPlacement};
    }
    budget.work-=sendable_work;
    if(tree.sendable) {
      result.arkts=true;
      // Storage descriptions have no initializer. OH7's TS transform erases
      // these descriptions while retaining every constructor [[Set]]. Do not
      // enable --use-define-semantic when recompiling this output.
      constexpr std::array<std::pair<abc::SendableFieldType,std::string_view>,6> types{{
        {abc::SendableFieldType::kNumber,"number"},{abc::SendableFieldType::kString,"string"},
        {abc::SendableFieldType::kBoolean,"boolean"},{abc::SendableFieldType::kBigInt,"bigint"},
        {abc::SendableFieldType::kNull,"null"},{abc::SendableFieldType::kUndefined,"undefined"}}};
      for(std::size_t i=0;i<tree.sendable->fields.fields.size();++i) {
        if(!budget.work)throw Failure::kWorkLimit;--budget.work;
        for(std::size_t level=0;level<=indent;++level)append("  ");
        const auto& field=tree.sendable->fields.fields[i];
        if(field.placement==abc::SendableFieldPlacement::kStatic)append("static ");
        const auto search=std::bit_width(tree.sendable->field_names.size())+1;
        if(search>budget.work)throw Failure::kWorkLimit;budget.work-=search;
        const auto& key=*tree.sendable->FieldName(i);
        if(key.size()>budget.work)throw Failure::kWorkLimit;budget.work-=key.size();
        if(ClassifyMemberKey(key)==MemberKeySyntax::kIdentifier) {
          for(const auto unit:key){const char c=static_cast<char>(unit);append({&c,1});}
        } else {
          const auto printed=WriteStringLiteral(key,output,{limit.bytes-result.bytes});
          if(const auto* bytes=std::get_if<std::size_t>(&printed))result.bytes+=*bytes;
          else switch(std::get<EmitError>(printed)) {
            case EmitError::kOutputLimit:throw Failure::kOutputLimit;
            case EmitError::kWrite:throw Failure::kWrite;
            default:throw Failure::kInvalidLiteral;
          }
        }
        append(": ");bool first=true;
        if(field.type.bits==static_cast<unsigned>(abc::SendableFieldType::kGeneric)) {
          if(std::bit_width(tree.sendable_types.size())+1>budget.work)throw Failure::kWorkLimit;
          budget.work-=std::bit_width(tree.sendable_types.size())+1;
          const auto found=std::lower_bound(tree.sendable_types.begin(),tree.sendable_types.end(),i,[](const auto& item,auto ordinal){return item.field<ordinal;});
          if(found==tree.sendable_types.end()||found->field!=i)throw Failure::kInvalidLiteral;
          const auto& family=tree.constructor->Family();const auto& body=family.members[family.root.value].tree;
          if(found->depth>budget.bytes/sizeof(NameId))throw Failure::kResourceLimit;
          budget.bytes-=found->depth*sizeof(NameId);
          std::vector<NameId> path;path.reserve(found->depth);
          if(path.capacity()-found->depth>budget.bytes/sizeof(NameId))throw Failure::kResourceLimit;
          budget.bytes-=(path.capacity()-found->depth)*sizeof(NameId);
          auto at=found->constructor;
          for(std::uint32_t n=0;n<found->depth;++n) {
            if(!budget.work)throw Failure::kWorkLimit;--budget.work;
            const auto& member=std::get<MemberExpr>(body.expressions[at.value].expression);
            path.push_back(std::get<NameId>(member.key));at=member.base;
          }
          const auto emit_name=[&](NameId name){const auto& text=body.names[name.value];if(text.size()>budget.work)throw Failure::kWorkLimit;budget.work-=text.size();for(const auto unit:text){const char c=static_cast<char>(unit);append({&c,1});}};
          emit_name(body.symbols[found->imported.value].name);
          for(auto part=path.rbegin();part!=path.rend();++part){append(".");emit_name(*part);}
          first=false;
        }
        for(const auto& [bit,name]:types)if(field.type.bits & static_cast<unsigned>(bit)) {
          if(!first)append(" | ");first=false;append(name);
        }
        append(";\n");
      }
    }
    member(*tree.constructor);
    const auto omitted_member = [&](const ClassMemberOmission& omitted) {
      append("; method "); number(omitted.context.Target().value);
      append("; failed method "); number(omitted.method.value);
      if (omitted.parent) {
        append("; parent "); number(omitted.parent->value);
        append("; creation PC "); number(omitted.creation_pc->value);
      }
      append(std::holds_alternative<PhasedControlFailure>(omitted.failure) ? "; control proof PC " : "; input proof PC ");
      number(omitted.Pc().value);
      append(" */\n"); result.incomplete = true;
    };
    std::size_t gap = 0;
    for (std::size_t i = 0; i < tree.members.size(); ++i) {
      if (!tree.members[i]) {
        const auto& omitted = tree.omissions[gap++];
        // Only numeric identities enter this diagnostic comment. No unknown
        // body/signature, user-controlled comment text or source span is faked.
        for (std::size_t level = 0; level <= indent; ++level) append("  ");
        append("/* UNRECOVERED class member "); number(i);
        omitted_member(omitted);
        continue;
      }
      const auto& family = *tree.members[i]; const auto* site = family.ClassCreation();
      if (!site || family.Identity() != tree.identity || site->creator != tree.creation.creator ||
          site->pc != tree.creation.pc || site->property != i) throw Failure::kUnsupportedContext;
      member(family);
    }
    std::optional<DefinitionOrder> previous_definition;
    std::size_t installation = 0;
    if (tree.installation_order &&
        (!tree.initializer ||
         tree.installation_order->initializer != tree.initializer->call ||
         tree.installation_order->definitions.size() != tree.definitions.size()))
      throw Failure::kUnsupportedContext;
    for (const auto& accessor : tree.definitions) {
      const bool ordered = tree.installation_order &&
          tree.installation_order->definitions[installation++] == accessor.site.definition;
      if ((tree.initializer && !ordered &&
           (!tree.class_self || !tree.class_self->certificate)) ||
          (previous_definition && *previous_definition >= accessor.site.order))
        throw Failure::kUnsupportedContext;
      previous_definition = accessor.site.order;
      const auto emit_member = [&](const PreparedFamily& family, abc::ClassMethodKind kind) {
        const auto* site = family.DefinedMemberCreation();
        if (!site || family.Identity() != tree.identity || site->function.creator != tree.creation.creator ||
            site->definition != accessor.site.definition || site->kind != kind ||
            site->placement != (accessor.site.prototype ? abc::ClassPlacement::kPrototype : abc::ClassPlacement::kStatic))
          throw Failure::kUnsupportedContext;
        member(family);
      };
      const auto methods = accessor.site.Methods();
      for (std::size_t i = 0; i < methods.size(); ++i) {
        if (accessor.bodies[i]) emit_member(*accessor.bodies[i], methods[i].kind);
        else if (accessor.omissions[i]) {
          for (std::size_t level = 0; level <= indent; ++level) append("  ");
          append("/* UNRECOVERED class definition "); number(accessor.site.definition.value);
          append("; slot "); number(i);
          omitted_member(*accessor.omissions[i]);
        }
      }
    }
    if (tree.initializer) {
      const auto& initializer = *tree.initializer;
      const auto* site = initializer.context.CreationSite();
      if (!site || initializer.tree.identity != tree.identity || initializer.tree.method != site->target ||
          site->creator != tree.creation.creator || initializer.context.Callable() != CallableKind::kMethod)
        throw Failure::kUnsupportedContext;
      const auto globals_check = VerifyTreeGlobals(initializer.tree, globals, budget);
      const auto* checked = std::get_if<TreeGlobals>(&globals_check);
      if (!checked) {
        const auto error = std::get<TreeGlobalError>(globals_check);
        if (error == TreeGlobalError::kResourceLimit) throw Failure::kResourceLimit;
        if (error == TreeGlobalError::kWorkLimit) throw Failure::kWorkLimit;
        throw Failure::kUnsupportedContext;
      }
      if (checked->allocated_bytes > budget.bytes) {PrintByteCharge(checked->allocated_bytes,1,budget.bytes,initial.bytes,initial.bytes-budget.bytes);throw Failure::kResourceLimit;}
      if (checked->work > budget.work) {PrintCharge(TreePrintBudgetKind::kWork,checked->work,budget.work,initial.work);throw trace.Capture(Failure::kWorkLimit);}
      budget.bytes -= checked->allocated_bytes; budget.work -= checked->work;
      auto printed = PrintStaticFields(initializer.tree, output, {limit.bytes - result.bytes}, budget, indent + 1, globals, initializer.record,initializer.flow.original.get());
      if (const auto* error = std::get_if<TreePrintFailure>(&printed)) throw *error;
      auto positions = std::get<PrintedFunction>(std::move(printed));
      if (positions.allocated_bytes > budget.bytes) {PrintByteCharge(positions.allocated_bytes,1,budget.bytes,initial.bytes,initial.bytes-budget.bytes);throw Failure::kResourceLimit;}
      if (positions.work > budget.work) {PrintCharge(TreePrintBudgetKind::kWork,positions.work,budget.work,initial.work);throw trace.Capture(Failure::kWorkLimit);}
      budget.bytes -= positions.allocated_bytes; budget.work -= positions.work;
      positions.enclosing_offset = result.bytes; result.bytes += positions.bytes;
      result.initializer = std::move(positions);
    }
    for (std::size_t i = 0; i < indent; ++i) append("  ");
    append("}");
    if (form == ClassPrintForm::kExpression) { append("})["); name(); append("]"); }
    else if (form == ClassPrintForm::kNamedExpression) append(")");
    result.allocated_bytes = initial.bytes - budget.bytes;
    result.work = initial.work - budget.work;
    return result;
  } catch (const TreePrintFailure& failure) { return failure; }
    catch (Failure failure) { return trace.Capture(failure); }
    catch (const FamilyPrepareFailure& failure) { trace.Propagate();return failure; }
    catch (const std::bad_alloc&) { PrintCharge(TreePrintBudgetKind::kHostAllocation,{},0,0); return trace.Capture(Failure::kResourceLimit); }
    catch (const std::length_error&) { PrintCharge(TreePrintBudgetKind::kCapacityOverflow,{},0,0); return trace.Capture(Failure::kResourceLimit); }
    catch (const std::ios_base::failure&) { return Failure::kWrite; }
}
}  // namespace
std::variant<PrintedClassTree, TreePrintFailure, FamilyPrepareFailure> PrintClassTree(
    const ClassTree& tree, std::ostream& output, EmitBudget limit,
    analysis::SemanticBudget budget, const GlobalScope* globals, std::size_t indent) {
  TreePrintTrace trace;trace.failure.site=TreePrintSite::kClassNamePlanning;trace.failure.location.method=tree.creation.target;
  auto planned = PlanPrintedClassNames(tree, globals, budget);
  if (const auto* error = std::get_if<TreeStructureError>(&planned)) {
    if (*error == TreeStructureError::kResourceLimit) return trace.Capture(TreePrintError::kResourceLimit);
    if (*error == TreeStructureError::kWorkLimit) return trace.Capture(TreePrintError::kWorkLimit);
    return TreePrintError::kUnsupportedContext;
  }
  const auto& names = std::get<ClassNames>(planned);
  const auto name = names.Lookup(tree);
  if (names.allocated_bytes > budget.bytes) {PrintByteCharge(names.allocated_bytes,1,budget.bytes,budget.bytes);return trace.Capture(TreePrintError::kResourceLimit);}
  if (names.work > budget.work || name.work > budget.work - names.work) return trace.Capture(TreePrintError::kWorkLimit);
  budget.bytes -= names.allocated_bytes; budget.work -= names.work + name.work;
  auto result = PrintClassPart(tree, output, limit, budget, globals, indent,
      name.named ? ClassPrintForm::kNamedExpression : ClassPrintForm::kExpression, nullptr, &names);
  if (auto* printed = std::get_if<PrintedClassTree>(&result)) {
    printed->allocated_bytes += names.allocated_bytes;
    printed->work += names.work + name.work;
  }
  return result;
}
}  // namespace abcd::source
