#include "module_completion.h"
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
#include "tree_globals.h"
#include "family_print.h"
#include "class_tree.h"
#include "family_enclosure.h"
#include "family_route_failure.h"
#include "class_tree_context.h"
#include "tree_checks.h"
#include "member_key.h"
#include "module_default_export.h"

namespace abcd::source {
namespace {
using Error = TreePrintError;
enum class ClassPrintForm { kExpression, kDerivedBody };
std::variant<PrintedClassTree, TreePrintError, FamilyPrepareFailure> PrintClassPart(
    const ClassTree&, std::ostream&, EmitBudget, analysis::SemanticBudget,
    const GlobalScope*, std::size_t, ClassPrintForm, const ClassCaptureDestination* = nullptr);
struct DerivedClassBody { std::shared_ptr<const ClassTree> definition; ExprId expression; };
struct EndExpression { ExprId id; };
struct ExpressionOperand { ExprId id; TreeOperatorContext context; };
struct Statement { StmtId id; std::size_t depth; };
struct EndStatement { StmtId id; };
struct BlockCursor { StmtId id; std::uint32_t index; std::size_t depth; };
struct Indent { std::size_t depth; };
struct PropertyKey { NameId id; };
struct SymbolName { SymbolId id; };
struct ArgumentCursor { ArgumentRange range; std::uint32_t index; };
struct SnapshotCursor { ArgumentRange range; std::uint32_t index; };
struct ObjectCursor { PropertyRange range; std::uint32_t index; AggregateKind kind = AggregateKind::kObject; };
struct EndProperty { PropertyId id; };
struct BeginFunction {};
struct EndFunction {};
struct ParameterCursor { std::uint32_t index; };
struct EndParameter { std::uint32_t index; };
using PrintAction = std::variant<std::string_view, ExprId, EndExpression, ExpressionOperand,
    Statement, EndStatement, BlockCursor, Indent, PropertyKey, SymbolName, ArgumentCursor,
    ObjectCursor, EndProperty, BeginFunction, EndFunction, ParameterCursor, EndParameter, SnapshotCursor, DerivedClassBody>;
struct OwnedAction { std::size_t owner; std::size_t depth; PrintAction action; };
struct LocatedAnchor { BlockBoundary boundary; AnchorId id; };
struct OwnerTree { FunctionOwnerId owner; std::size_t member; };

class TreePrinter {
 public:
  TreePrinter(const FunctionTree& tree, std::ostream& output,
              EmitBudget limit, analysis::SemanticBudget scratch, const GlobalScope* scope)
      : tree_(&tree), output_(output), limit_(limit.bytes), initial_(scratch),
        remaining_(scratch), scope_(scope) {}
  TreePrinter(const VerifiedFunction& function, std::ostream& output,
              EmitBudget limit, analysis::SemanticBudget scratch, const GlobalScope* scope)
      : function_(&function), tree_(&function.Tree()), output_(output),
        limit_(limit.bytes), initial_(scratch), remaining_(scratch), scope_(scope) {}
  TreePrinter(const ConstructedFamily& family, std::ostream& output,
              EmitBudget limit, analysis::SemanticBudget scratch, const ClassTreeContext* member,
              const GlobalScope* scope)
      : family_(&family), class_root_(member), output_(output), limit_(limit.bytes), initial_(scratch), remaining_(scratch), scope_(scope) {}
  PrintedFunction Print() {
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
      if (std::holds_alternative<ArgumentSnapshotExpr>(expression.expression)) RecordArgumentSnapshot();
    }
    Result().identity = Tree().identity;
    Result().method = Tree().method;
    Result().value_revision = Tree().value_revision;
    Result().cfg_revision = Tree().cfg_revision;
    if (!Tree().modules.empty())
      Result().module_binding = PrintedFunction::ModuleBinding{
          *function_->Receipt().record, scope_->module_enclosure->Owner()};
    Array(Result().parameters, Tree().parameters.size());
    Array(Result().expressions, Tree().expressions.size());
    Array(Result().properties, Tree().properties.size());
    Array(Result().statements, Tree().statements.size());
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
            (!std::holds_alternative<TreeLiteral>(Tree().expressions[parameter.initializer->value].expression) &&
             !std::holds_alternative<SymbolExpr>(Tree().expressions[parameter.initializer->value].expression)))
          throw Error::kUnsupportedContext;
        Append(" = ");
        Push(*parameter.initializer);
        Drain();
      }
      Result().parameters[i].end = written_;
    }
    Append(") ");
    Push(Statement{Tree().body, 0});
    Drain();
    for (std::size_t i = 0; i < Tree().anchors.size(); ++i) {
      Step();
      if (std::holds_alternative<FunctionExit>(Tree().anchors[i].position))
        Result().anchors[i] = written_;
    }
    Append("\n");
    result_.bytes = written_;
    Result().allocated_bytes = initial_.bytes - remaining_.bytes;
    Result().work = initial_.work - remaining_.work;
    return std::move(result_);
  }
  PrintedFunction Direct(bool module = false, bool fields = false, std::size_t indent = 0,
      std::optional<ModuleDefaultExport> default_export = {}) {
    default_export_ = default_export;
    module_body_ = module || fields;
    static_fields_ = fields;
    const auto checked = VerifyTreeStructure(Tree(), remaining_, scope_);
    if (const auto* error = std::get_if<TreeStructureError>(&checked)) {
      if (*error == TreeStructureError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == TreeStructureError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kUnsupportedContext;
    }
    const auto& structure = std::get<TreeStructure>(checked);
    Admit(structure.allocated_bytes, 1); Step(structure.work);
    if (Tree().callable != CallableKind::kFunction && !IsArrowCallable(Tree().callable) &&
        Tree().callable != CallableKind::kAsyncFunction && Tree().callable != CallableKind::kGenerator &&
        Tree().callable != CallableKind::kAsyncGenerator && !(fields && Tree().callable == CallableKind::kMethod))
      throw Error::kUnsupportedContext;
    if (fields) {
      const auto* body = std::get_if<BlockStmt>(&Tree().statements[Tree().body.value].statement);
      if (!body || !body->child_count || !Tree().parameters.empty() ||
          !Tree().captures.empty() || Tree().inferred_name || Tree().self ||
          !Tree().recovery_issues.empty() || Tree().callable != CallableKind::kMethod ||
          Tree().strictness != Strictness::kStrict) throw Error::kUnsupportedContext;
      for (const auto& symbol : Tree().symbols) {
        Step(); if (symbol.kind != BindingKind::kModule) throw Error::kUnsupportedContext;
      }
      std::size_t field_count = 0;
      for (const auto& expression : Tree().expressions) {
        Step(); field_count += std::holds_alternative<FieldDefinitionExpr>(expression.expression);
      }
      if (field_count != body->child_count - 1) throw Error::kUnsupportedContext;
      for (std::uint32_t i = 0; i < body->child_count; ++i) {
        Step(); const auto& node = Tree().statements[Tree().children[body->first_child + i].value].statement;
        if (i + 1 == body->child_count) {
          const auto* returned = std::get_if<ReturnStmt>(&node);
          if (!returned) throw Error::kUnsupportedContext;
          if (returned->value) {
            const auto* literal = std::get_if<TreeLiteral>(&Tree().expressions[returned->value->value].expression);
            if (!literal || !std::holds_alternative<UndefinedLiteral>(*literal)) throw Error::kUnsupportedContext;
          }
        } else {
          const auto* statement = std::get_if<ExpressionStmt>(&node);
          if (!statement || !std::holds_alternative<FieldDefinitionExpr>(Tree().expressions[statement->expression.value].expression))
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
      if (std::holds_alternative<ArgumentSnapshotExpr>(node.expression)) RecordArgumentSnapshot();
    }
    Result().identity = Tree().identity; Result().method = Tree().method;
    Result().value_revision = Tree().value_revision; Result().cfg_revision = Tree().cfg_revision;
    Result().fragment = fields ? FunctionFragment::kStaticFields :
        module ? FunctionFragment::kModuleBody : FunctionFragment::kConditionalExpression;
    Result().incomplete = !Tree().recovery_issues.empty();
    Array(Result().parameters, Tree().parameters.size()); Array(Result().expressions, Tree().expressions.size());
    Array(Result().properties, Tree().properties.size()); Array(Result().statements, Tree().statements.size());
    Array(Result().anchors, Tree().anchors.size()); PrepareAnchors();
    if (module || fields) Push(Statement{Tree().body, indent}); else Push(BeginFunction{});
    Drain(); Append("\n");
    Result().bytes = written_;
    Result().allocated_bytes = initial_.bytes - remaining_.bytes;
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
    Array(draft_.members, family_->members.size()); Array(draft_.functions, family_->members.size());
    Array(locations_, family_->members.size()); Array(owners_, family_->members.size());
    Array(printed_, family_->members.size());
    for (std::size_t i = 0; i < family_->members.size(); ++i) {
      current_ = i; Step();
      const auto checked = VerifyTreeStructure(Tree(), remaining_, scope_);
      const auto* structure = std::get_if<TreeStructure>(&checked);
      if (!structure) {
        switch (std::get<TreeStructureError>(checked)) {
          case TreeStructureError::kResourceLimit: throw Error::kResourceLimit;
          case TreeStructureError::kWorkLimit: throw Error::kWorkLimit;
          default: throw Error::kUnsupportedContext;
        }
      }
      Admit(structure->allocated_bytes, 1); Step(structure->work);
      if (Tree().strictness != Strictness::kStrict ||
          (Tree().callable != CallableKind::kFunction && !IsArrowCallable(Tree().callable) &&
           !IsMethodCallable(Tree().callable) && Tree().callable != CallableKind::kBaseConstructor && Tree().callable != CallableKind::kDerivedConstructor && Tree().callable != CallableKind::kAsyncFunction &&
           Tree().callable != CallableKind::kGenerator && Tree().callable != CallableKind::kAsyncGenerator))
        throw Error::kUnsupportedContext;
      // Only the actual class root is a class member. An arrow method carrier
      // retains its original callable kind and cannot observe its home object.
      if ((IsMethodCallable(Tree().callable) || Tree().callable == CallableKind::kBaseConstructor || Tree().callable == CallableKind::kDerivedConstructor) && !ClassRoot())
        throw Error::kUnsupportedContext;
      // Naming adds a fresh, unobservable wrapper object. Its allocation-failure
      // timing is admitted only by the already-versioned resource-failure scope.
      if (Tree().inferred_name && (!Tree().self || Tree().self_form == SelfForm::kCreationBinding) &&
          ResourceFailures(Tree().identity.contract) != ResourceFailureScope::kExcluded)
        throw Error::kUnsupportedContext;
      owners_[i] = {Tree().owner, i};
      Result().identity = Tree().identity; Result().method = Tree().method;
      Result().value_revision = Tree().value_revision; Result().cfg_revision = Tree().cfg_revision;
      if (ModuleBody()) { CheckModuleBody(); CheckDefaultExport(); }
      Result().fragment = ModuleBody() ? FunctionFragment::kModuleBody :
          ClassRoot() ? FunctionFragment::kClassMember : FunctionFragment::kConditionalExpression;
      Result().incomplete = !Tree().recovery_issues.empty();
      if (!Tree().modules.empty() && scope_ && scope_->module_enclosure) {
        if (!family_->members[i].record) throw Error::kUnsupportedContext;
        has_external_binding_ = true;
        Result().module_binding = PrintedFunction::ModuleBinding{
            *family_->members[i].record, scope_->module_enclosure->Owner()};
      }
      Array(Result().parameters, Tree().parameters.size()); Array(Result().expressions, Tree().expressions.size());
      Array(Result().properties, Tree().properties.size()); Array(Result().statements, Tree().statements.size());
      Array(Result().anchors, Tree().anchors.size()); PrepareAnchors();
      for (const auto& node : Tree().expressions) {
        Step();
        if (std::holds_alternative<RealmObjectExpr>(node.expression)) RecordRealm();
        if (std::holds_alternative<SymbolConstructorExpr>(node.expression)) RecordSymbol();
        if (std::holds_alternative<ArgumentSnapshotExpr>(node.expression)) RecordArgumentSnapshot();
      }
    }
    if (has_external_binding_) {
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
      Admit(enclosure->allocated_bytes, 1); Step(enclosure->work);
    }
    std::sort(owners_.begin(), owners_.end(), [&](const auto& a, const auto& b) { Step(); return a.owner < b.owner; });
    for (std::size_t i = 1; i < owners_.size(); ++i) {
      Step(); if (owners_[i - 1].owner == owners_[i].owner) throw Error::kUnsupportedContext;
    }
    PrepareFamilyStatus();
    current_ = family_->root.value; depth_ = indent;
    Push(BeginFunction{}); Drain(); Append("\n");
    for (const auto emitted : printed_) { Step(); if (!emitted) throw Error::kUnsupportedContext; }
    draft_.bytes = written_; draft_.allocated_bytes = initial_.bytes - remaining_.bytes;
    draft_.work = initial_.work - remaining_.work;
    return std::move(draft_);
  }
 private:
  bool ModuleBody() const { return module_body_ && (!family_ || current_ == family_->root.value); }
  void CheckDefaultExport() {
    if (!default_export_) return;
    const auto checked = CheckModuleDefaultExport(Tree(), family_, *default_export_, remaining_);
    if (const auto* error = std::get_if<Error>(&checked)) throw *error;
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
  void RecordArgumentSnapshot() {
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
      std::visit([&](const auto& value) { Run(value); }, action.action);
    }
  }
  void Step(std::uint64_t count = 1) {
    if (count > remaining_.work) throw Error::kWorkLimit;
    remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    if (count > remaining_.bytes / width) throw Error::kResourceLimit;
    remaining_.bytes -= count * width;
  }
  template <class T> void Array(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T)); Step(count);
    values.resize(count);
    Admit(values.capacity() - count, sizeof(T));
  }
  void Push(PrintAction action) {
    if (actions_.size() == actions_.capacity()) {
      // Geometric growth; retain replaced allocations in the charge, covering
      // the reserve peak without a guessed host-stack depth bound.
      constexpr std::size_t kInitialActions = 16;
      const auto old = actions_.capacity();
      if (old > std::numeric_limits<std::size_t>::max() / 2)
        throw Error::kResourceLimit;
      const auto capacity = old == 0 ? kInitialActions : old * 2;
      Admit(capacity, sizeof(OwnedAction));
      actions_.reserve(capacity);
      Admit(actions_.capacity() - capacity, sizeof(OwnedAction));
    }
    actions_.push_back({current_, depth_, action});
  }
  void Append(std::string_view text) {
    Step(text.size());
    if (text.size() > limit_ - written_) throw Error::kOutputLimit;
    output_.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output_) throw Error::kWrite;
    written_ += text.size();
  }
  void Quoted(std::u16string_view text) {
    Step(text.size());
    const auto written = WriteStringLiteral(text, output_, {limit_ - written_});
    if (const auto* bytes = std::get_if<std::size_t>(&written)) written_ += *bytes;
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
  void Name(SymbolId id) { Identifier(Tree().names[Tree().symbols[id.value].name.value]); }
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
      if (value.bits != std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN()))
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
    if (!Result().argument_snapshot_binding) throw Error::kUnsupportedContext;
    Append("(0, "); Identifier(Result().argument_snapshot_binding->name);
    Append(")(arguments, [");
    Push(std::string_view("])"));
    Push(SnapshotCursor{node.slots, 0});
  }
  void Member(ExprId base, const MemberKey& key) {
    if (const auto* name = std::get_if<NameId>(&key)) Push(PropertyKey{*name});
    else {
      Push(std::string_view("]"));
      Push(std::get<ComputedKey>(key).expression);
      Push(std::string_view("["));
    }
    const auto& expression = Tree().expressions[base.value].expression;
    const bool primary = std::holds_alternative<SymbolExpr>(expression) ||
        std::holds_alternative<ThisExpr>(expression) ||
        std::holds_alternative<MemberExpr>(expression);
    if (!primary) Push(std::string_view(")"));
    Push(base);
    if (!primary) Push(std::string_view("("));
  }
  void Expression(const MemberExpr& node) { Member(node.base, node.key); }
  void Expression(AwaitExpr node) {
    Append("(await "); Push(std::string_view(")"));
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
    // typeof must obtain a value, preserving unresolvable-name exceptions.
    if (node.operation == TreeUnary::kTypeof) {
      Append("(0, "); Push(std::string_view(")")); Push(node.operand);
    } else Push(ExpressionOperand{node.operand, {node.operation}});
  }
  void Expression(BinaryExpr node) {
    Push(ExpressionOperand{node.right, {node.operation, OperandSide::kRight}});
    Push(TreeBinaryToken(node.operation));
    Push(ExpressionOperand{node.left, {node.operation, OperandSide::kLeft}});
  }
  void Expression(LogicalExpr node) {
    Push(ExpressionOperand{node.right, {node.operation, OperandSide::kRight}});
    Push(TreeLogicalToken(node.operation));
    Push(ExpressionOperand{node.left, {node.operation, OperandSide::kLeft}});
  }
  void Expression(SequenceExpr node) {
    Append("("); Push(std::string_view(")")); Push(node.right);
    Push(std::string_view(", ")); Push(node.left);
  }
  void Expression(UpdateExpr node) {
    const auto token = node.direction == UpdateDirection::kIncrement ? "++" : "--";
    Append("("); Push(std::string_view(")"));
    if (node.result == UpdateResult::kPostfix) Push(std::string_view(token));
    Push(SymbolName{node.symbol});
    if (node.result == UpdateResult::kPrefix) Append(token);
  }
  void Expression(ConditionalExpr node) {
    Append("("); Push(std::string_view(")")); Push(node.alternate);
    Push(std::string_view(" : ")); Push(node.consequent);
    Push(std::string_view(" ? ")); Push(node.condition);
  }
  void Expression(const FieldDefinitionExpr& node) {
    if (!static_fields_) throw Error::kUnsupportedContext;
    const auto& name = Tree().names[node.name.value];
    Append("static ");
    if (ClassifyObjectKey(name) == MemberKeySyntax::kIdentifier && name != u"prototype" && name != u"constructor")
      Identifier(name);
    else { Append("["); Quoted(name); Append("]"); }
    Append(" = ");
    Push(node.value);
  }
  void Expression(const AssignmentExpr& node) {
    Append("("); Push(std::string_view(")")); Push(node.value);
    Push(std::string_view(" = "));
    if (const auto* symbol = std::get_if<SymbolTarget>(&node.target)) Push(SymbolName{symbol->symbol});
    else {
      const auto& member = std::get<MemberTarget>(node.target);
      Member(member.base, member.key);
    }
  }
  void Expression(const CallExpr& node) {
    if (node.reference == CallReference::kValue) {
      Append("(0, ");
      Push(std::string_view(")"));
      Push(ArgumentCursor{node.arguments, 0});
      Push(std::string_view(")("));
    } else {
      Append("(");
      Push(std::string_view("))"));
      Push(ArgumentCursor{node.arguments, 0});
      Push(std::string_view("("));
    }
    Push(ExpressionOperand{node.callee, {ExpressionPrecedence::kPrimary}});
  }
  void Expression(const ConstructExpr& node) {
    Append("(new (");
    Push(std::string_view("))"));
    Push(ArgumentCursor{node.arguments, 0});
    Push(std::string_view(")("));
    Push(node.constructor);
  }
  void Expression(const SuperCallExpr& node) {
    if (Tree().callable != CallableKind::kDerivedConstructor || !ClassRoot())
      throw Error::kUnsupportedContext;
    Append("super(");
    Push(std::string_view(")"));
    Push(ArgumentCursor{node.arguments, 0});
  }
  void Expression(EmptyArrayExpr) { Append("[]"); }
  void Expression(EmptyObjectExpr) { Append("({})"); }
  void Expression(const ClassExpr& node, ExprId expression) {
    // Only the already composed module root admits class definitions here.
    // Member families own their external root evidence. The already checked
    // parent syntax is passed explicitly, without recovering scope/CFG here.
    // Classes inside descendant functions still need their own composition.
    if ((family_ && !ModuleBody()) || !node.definition || node.definition->identity != Tree().identity ||
        node.definition->creation.creator != Tree().method) throw Error::kUnsupportedContext;
    if (node.heritage) {
      if (!node.definition->BodiesPrepared() || node.definition->callable != CallableKind::kDerivedConstructor)
        throw Error::kUnsupportedContext;
      Append("({["); Quoted(node.definition->name); Append("]: class extends (");
      Push(DerivedClassBody{node.definition, expression}); Push(*node.heritage);
    } else ClassPart(*node.definition, ClassPrintForm::kExpression, expression);
  }
  void Run(const DerivedClassBody& body) {
    Append(")");
    ClassPart(*body.definition, ClassPrintForm::kDerivedBody, body.expression);
    Append("})["); Quoted(body.definition->name); Append("]");
  }
  void ClassPart(const ClassTree& definition, ClassPrintForm form, ExprId expression) {
    const ClassCaptureDestination parent{Tree(), expression};
    auto emitted = PrintClassPart(definition, output_, {limit_ - written_}, remaining_, scope_, depth_, form, &parent);
    if (const auto* error = std::get_if<TreePrintError>(&emitted)) throw *error;
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
    Admit(positions.allocated_bytes, 1); Step(positions.work);
    const auto retain = [&](PrintedFunction function, std::size_t offset) {
      auto& retained = Result().class_members;
      if (retained.size() == retained.capacity()) {
        const auto capacity = retained.capacity();
        if (capacity > std::numeric_limits<std::size_t>::max() / 2) throw Error::kResourceLimit;
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
  void Expression(FunctionExpr node) {
    if (!family_) throw Error::kUnsupportedContext;
    const auto child = std::lower_bound(owners_.begin(), owners_.end(), node.child,
        [&](const auto& item, FunctionOwnerId owner) { Step(); return item.owner < owner; });
    if (child == owners_.end() || child->owner != node.child) throw Error::kUnsupportedContext;
    const auto parent = current_; current_ = child->member;
    Push(BeginFunction{}); current_ = parent;
  }
  void Run(BeginFunction) {
    if (family_) {
      if (printed_[current_]) throw Error::kUnsupportedContext;
      printed_[current_] = true; draft_.functions[current_].begin = written_;
    }
    if (Result().incomplete) {
      Append("/* INCOMPLETE: partial decompilation; not executable-equivalent. */");
      // Nested function expressions can follow return/throw. A newline inside
      // this annotation would insert a semicolon before the recovered value.
      Append(family_ && current_ != family_->root.value ? " " : "\n");
    }
    if (ModuleBody()) {
      Push(EndFunction{}); Push(Statement{Tree().body, depth_}); return;
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
      Append(Tree().callable == CallableKind::kAsyncGenerator ? "(async function* " :
          Tree().callable == CallableKind::kGenerator ? "(function* " :
          Tree().callable == CallableKind::kAsyncFunction ? "(async function " : "(function ");
      Name(*Tree().self); Append("(");
    } else if (Tree().inferred_name) {
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
      Append(Tree().invocation == InvocationForm::kArrowMethod && IsAsyncCallable(Tree().callable)
          ? "({async [" : "({[");
      Quoted(Tree().names[Tree().inferred_name->value]);
      if (Tree().invocation == InvocationForm::kArrowMethod) Append("](");
      else {
        Append("]: ");
        Append(Tree().callable == CallableKind::kAsyncArrow ? "async (" : Tree().callable == CallableKind::kArrow ? "(" :
            Tree().callable == CallableKind::kAsyncGenerator ? "async function*(" :
            Tree().callable == CallableKind::kGenerator ? "function*(" :
            Tree().callable == CallableKind::kAsyncFunction ? "async function(" : "function(");
      }
    } else Append(Tree().callable == CallableKind::kAsyncArrow ? "(async (" : Tree().callable == CallableKind::kArrow ? "((" :
        Tree().callable == CallableKind::kAsyncGenerator ? "(async function*(" :
        Tree().callable == CallableKind::kGenerator ? "(function*(" :
        Tree().callable == CallableKind::kAsyncFunction ? "(async function(" : "(function(");
    Push(EndFunction{}); Push(Statement{Tree().body, depth_});
    Push(std::string_view(IsArrowCallable(Tree().callable) &&
        Tree().invocation == InvocationForm::kDeclared ? ") => " : ") "));
    Push(ParameterCursor{0});
  }
  void Run(EndFunction) {
    for (std::size_t i = 0; i < Tree().anchors.size(); ++i) {
      Step(); if (std::holds_alternative<FunctionExit>(Tree().anchors[i].position)) Result().anchors[i] = written_;
    }
    // Class-body syntax closes with the block itself, not an expression wrap.
    if (!ClassRoot() && !ModuleBody()) {
      if (Tree().inferred_name && (!Tree().self || Tree().self_form == SelfForm::kCreationBinding)) {
        Append("})["); Quoted(Tree().names[Tree().inferred_name->value]); Append("]");
        if (Tree().self) { Append("; return "); Name(*Tree().self); Append("; })()"); }
      } else Append(")");
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
      if ((!std::holds_alternative<TreeLiteral>(Tree().expressions[parameter.initializer->value].expression) &&
             !std::holds_alternative<SymbolExpr>(Tree().expressions[parameter.initializer->value].expression)))
        throw Error::kUnsupportedContext;
      Append(" = "); Push(*parameter.initializer);
    }
  }
  void Run(EndParameter end) { Result().parameters[end.index].end = written_; }
  void Expression(ObjectExpr node) {
    const bool array = node.kind == AggregateKind::kArray;
    Append(array ? "[" : "({"); Push(std::string_view(array ? "]" : "})"));
    Push(ObjectCursor{node.properties, 0, node.kind});
  }
  void Run(ObjectCursor cursor) {
    if (cursor.index == cursor.range.count) return;
    if (cursor.index != 0) Append(", ");
    const PropertyId id{cursor.range.first + cursor.index};
    const auto& property = Tree().properties[id.value];
    Result().properties[id.value].begin = written_;
    if (cursor.kind == AggregateKind::kObject) {
      const auto& name = Tree().names[property.key.value];
      Step(name.size());
      // Ordinary IdentifierName keys keep native object syntax. __proto__ must
      // remain computed: a colon definition otherwise mutates the prototype.
      if (ClassifyObjectKey(name) == MemberKeySyntax::kIdentifier) {
        Identifier(name); Append(": ");
      } else {
        Append("["); Quoted(name); Append("]: ");
      }
    }
    Push(ObjectCursor{cursor.range, cursor.index + 1, cursor.kind});
    Push(EndProperty{id}); Push(property.value);
  }
  void Run(EndProperty property) { Result().properties[property.id.value].end = written_; }
  void Run(ArgumentCursor cursor) {
    if (cursor.index == cursor.range.count) return;
    if (cursor.index != 0) Append(", ");
    Push(ArgumentCursor{cursor.range, cursor.index + 1});
    Push(Tree().arguments[cursor.range.first + cursor.index]);
  }
  void Run(SnapshotCursor cursor) {
    if (cursor.index == cursor.range.count) return;
    if (cursor.index != 0) Append(", ");
    Append("(arguments.length > "); Numeric(cursor.index); Append(" ? ");
    Push(SnapshotCursor{cursor.range, cursor.index + 1});
    Push(std::string_view(" : void 0)"));
    Push(Tree().arguments[cursor.range.first + cursor.index]);
  }
  void Run(std::string_view token) { Append(token); }
  void Run(SymbolName symbol) { Name(symbol.id); }
  void Run(PropertyKey key) {
    const auto& name = Tree().names[key.id.value];
    Step(name.size());
    if (IsSourceIdentifier(name)) { Append("."); Identifier(name); }
    else { Append("["); Quoted(name); Append("]"); }
  }
  void Run(ExprId id) { Run(ExpressionOperand{id, {}}); }
  void Run(ExpressionOperand operand) {
    const auto id = operand.id;
    Result().expressions[id.value].begin = written_;
    Push(EndExpression{id});
    if (TreeOperandGrouping(Tree().expressions[id.value].expression, operand.context)) {
      Append("("); Push(std::string_view(")"));
    }
    std::visit([&](const auto& node) {
      if constexpr (std::is_same_v<std::decay_t<decltype(node)>, ClassExpr>) Expression(node, id);
      else Expression(node);
    }, Tree().expressions[id.value].expression);
  }
  void Run(EndExpression end) { Result().expressions[end.id.value].end = written_; }
  void Run(Indent indent) {
    constexpr std::string_view kIndent = "  ";
    for (std::size_t i = 0; i < indent.depth; ++i) Append(kIndent);
  }
  void StatementNode(const BlockStmt&, Statement at) {
    if (!ModuleBody() || at.id != Tree().body) Append("{\n");
    Push(BlockCursor{at.id, 0, at.depth});
  }
  void StatementNode(const DeclarationStmt& node, Statement at) {
    if (ModuleBody() && default_export_ && at.id == default_export_->declaration) {
      Append("export default "); Push(std::string_view(";")); Push(*node.initializer);
      return;
    }
    switch (Tree().symbols[node.symbol.value].declaration) {
      case DeclarationKind::kLet: Append("let "); break;
      case DeclarationKind::kConst: Append("const "); break;
      case DeclarationKind::kParameter: case DeclarationKind::kCatchParameter:
        throw Error::kUnsupportedContext;
    }
    Name(node.symbol); Push(std::string_view(";"));
    if (node.initializer) { Append(" = "); Push(*node.initializer); }
  }
  void StatementNode(ExpressionStmt node, Statement) {
    Push(std::string_view(";")); Push(node.expression);
  }
  void StatementNode(const IfStmt& node, Statement at) {
    Append("if (");
    if (node.alternate) {
      Push(Statement{*node.alternate, at.depth}); Push(std::string_view(" else "));
    }
    Push(Statement{node.consequent, at.depth}); Push(std::string_view(") "));
    Push(node.condition);
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
      Push(*node.value);
    }
  }
  void StatementNode(ThrowStmt node, Statement) {
    Append("throw "); Push(std::string_view(";")); Push(node.value);
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
  void StatementNode(LoopStmt node, Statement at) {
    if (node.label) { Label(*node.label); Append(": "); }
    Append("while (true) ");
    Push(Statement{node.body, at.depth});
  }
  void Label(LabelId label) { Append("$loop"); Numeric(label.value); }
  void Transfer(std::string_view token, std::optional<LabelId> label) {
    Append(token);
    if (label) { Append(" "); Label(*label); }
    Append(";");
  }
  void StatementNode(BreakStmt node, Statement) { Transfer("break", node.label); }
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
    std::visit([&](const auto& node) { StatementNode(node, at); }, Tree().statements[at.id.value].statement);
  }
  void Run(EndStatement end) { Result().statements[end.id.value].end = written_; }
  void Run(BlockCursor at) {
    const auto& block = std::get<BlockStmt>(Tree().statements[at.id.value].statement);
    Boundary({at.id, at.index});
    const bool root = ModuleBody() && at.id == Tree().body;
    if (at.index == block.child_count) { if (!root) { Run(Indent{at.depth}); Append("}"); } return; }
    Push(BlockCursor{at.id, at.index + 1, at.depth});
    if (root && at.index + 1 == block.child_count) {
      if (const auto completion = PureModuleCompletion(Tree())) {
        // An actual zero-width module-end event, not an invented expression
        // range. Position consumers validate/serialize its omitted meaning.
        Result().statements[completion->statement.value] = {written_, written_};
        if (completion->value) Result().expressions[completion->value->value] = {written_, written_};
        return;
      }
    }
    Push(std::string_view("\n"));
    const auto depth = root ? at.depth : at.depth + 1;
    Push(Statement{Tree().children[block.first_child + at.index], depth});
    Push(Indent{depth});
  }
  void PrepareAnchors() {
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
  void Boundary(BlockBoundary position) {
    // Binary lookup per structural boundary, not a scan of every method anchor.
    auto found = std::lower_bound(Located().begin(), Located().end(), position,
        [&](const auto& item, auto key) { Step(); return Less(item.boundary, key); });
    while (found != Located().end() && !Less(position, found->boundary)) {
      Step(); Result().anchors[found->id.value] = written_; ++found;
    }
  }
  const VerifiedFunction* function_ = nullptr;
  const FunctionTree* tree_ = nullptr;
  const ConstructedFamily* family_ = nullptr;
  const ClassTreeContext* class_root_ = nullptr;
  std::ostream& output_;
  std::size_t limit_;
  analysis::SemanticBudget initial_;
  analysis::SemanticBudget remaining_;
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
  std::vector<LocatedAnchor> located_;
  std::vector<std::vector<LocatedAnchor>> locations_;
  std::vector<OwnerTree> owners_;
  std::vector<std::uint8_t> printed_;
};
}  // namespace

std::variant<PrintedFunction, TreePrintError> PrintStaticFields(
    const FunctionTree& tree, std::ostream& output, EmitBudget limit, analysis::SemanticBudget budget, std::size_t indent,
    const GlobalScope* scope, std::optional<abc::EntityOffset> record) {
  try {
    if (!tree.modules.empty() && !record) return Error::kUnsupportedContext;
    const auto destination = VerifyModuleDestination(tree, record.value_or(abc::EntityOffset{}), scope, budget);
    const auto* verified = std::get_if<ModuleDestination>(&destination);
    if (!verified) {
      const auto error = std::get<ModuleEnclosureError>(destination);
      if (error == ModuleEnclosureError::kResourceLimit) return Error::kResourceLimit;
      if (error == ModuleEnclosureError::kWorkLimit) return Error::kWorkLimit;
      return Error::kUnsupportedContext;
    }
    budget.work -= verified->work;
    auto result = TreePrinter(tree, output, limit, budget, scope).Direct(false, true, indent);
    result.work += verified->work;
    if (!tree.modules.empty()) result.module_binding = PrintedFunction::ModuleBinding{*record, scope->module_enclosure->Owner()};
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit;
  } catch (const std::ios_base::failure&) { return Error::kWrite; }
}

std::variant<PrintedFunction, TreePrintError> PrintFunctionTree(
    const FunctionTree& tree, std::ostream& output,
    EmitBudget limit, analysis::SemanticBudget scratch, const GlobalScope* scope) {
  try { return TreePrinter(tree, output, limit, scratch, scope).Direct();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit;
  } catch (const std::ios_base::failure&) { return Error::kWrite; }
}

std::variant<PrintedFunction, TreePrintError> PrintModuleTree(
    const FunctionTree& tree, std::ostream& output,
    EmitBudget limit, analysis::SemanticBudget scratch, const GlobalScope* scope, std::optional<ModuleDefaultExport> default_export) {
  try { return TreePrinter(tree, output, limit, scratch, scope).Direct(true, false, 0, default_export);
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit;
  } catch (const std::ios_base::failure&) { return Error::kWrite; }
}

std::variant<PrintedFamilyDraft, TreePrintError> PrintFamilyDraft(const ConstructedFamily& family,
    std::ostream& output, EmitBudget limit, analysis::SemanticBudget scratch, const GlobalScope* scope) {
  try { return TreePrinter(family, output, limit, scratch, nullptr, scope).Draft();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit;
  } catch (const std::ios_base::failure&) { return Error::kWrite; }
}
std::variant<PrintedFamilyDraft, TreePrintError> PrintModuleFamilyDraft(const ConstructedFamily& family,
    std::ostream& output, EmitBudget limit, analysis::SemanticBudget scratch, const GlobalScope* scope, std::optional<ModuleDefaultExport> default_export) {
  try { return TreePrinter(family, output, limit, scratch, nullptr, scope).Draft(0, true, default_export);
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit;
  } catch (const std::ios_base::failure&) { return Error::kWrite; }
}
std::variant<PrintedFamilyDraft, TreePrintError> PrintClassFamilyDraft(const ConstructedFamily& family,
    const ClassTreeContext& context, std::ostream& output, EmitBudget limit, analysis::SemanticBudget scratch, const GlobalScope* scope, std::size_t indent) {
  try { return TreePrinter(family, output, limit, scratch, &context, scope).Draft(indent);
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit;
  } catch (const std::ios_base::failure&) { return Error::kWrite; }
}

std::variant<PrintedFunction, TreePrintError> PrintFunctionTree(
    const VerifiedFunction& function, std::ostream& output,
    EmitBudget output_budget, analysis::SemanticBudget scratch, const GlobalScope* scope) {
  try {
    if (!function.Receipt().record) return Error::kUnsupportedContext;
    const auto destination = VerifyModuleDestination(function.Tree(), *function.Receipt().record, scope, scratch);
    const auto* modules = std::get_if<ModuleDestination>(&destination);
    if (!modules) {
      const auto error = std::get<ModuleEnclosureError>(destination);
      if (error == ModuleEnclosureError::kResourceLimit) return Error::kResourceLimit;
      if (error == ModuleEnclosureError::kWorkLimit) return Error::kWorkLimit;
      return Error::kUnsupportedContext;
    }
    scratch.work -= modules->work;
    const auto globals = VerifyTreeGlobals(function.Tree(), scope, scratch);
    const auto* checked = std::get_if<TreeGlobals>(&globals);
    if (!checked) {
      const auto error = std::get<TreeGlobalError>(globals);
      if (error == TreeGlobalError::kResourceLimit) return Error::kResourceLimit;
      if (error == TreeGlobalError::kWorkLimit) return Error::kWorkLimit;
      return Error::kUnsupportedContext;
    }
    scratch.work -= checked->work;
    auto result = TreePrinter(function, output, output_budget, scratch, scope).Print();
    result.work += checked->work + modules->work;
    result.allocated_bytes = std::max(result.allocated_bytes, checked->allocated_bytes);
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit;
  } catch (const std::ios_base::failure&) { return Error::kWrite; }
}
}  // namespace abcd::source

namespace abcd::source {
namespace {
std::variant<PrintedClassTree, TreePrintError, FamilyPrepareFailure> PrintClassPart(
    const ClassTree& tree, std::ostream& output, EmitBudget limit,
    analysis::SemanticBudget budget, const GlobalScope* globals, std::size_t indent, ClassPrintForm form,
    const ClassCaptureDestination* parent) {
  using Failure = TreePrintError;
  const auto initial = budget;
  try {
    if (!tree.BodiesPrepared() || ResourceFailures(tree.identity.contract) != ResourceFailureScope::kExcluded ||
        tree.constructor->Identity() != tree.identity ||
        tree.constructor->Family().members[tree.constructor->Family().root.value].tree.method != tree.creation.target)
      return Failure::kUnsupportedContext;
    // Only the enclosing ClassExpr may emit a derived header/heritage. The
    // public whole-class route remains restricted to actual base constructors.
    if (tree.constructor->Family().members[tree.constructor->Family().root.value].tree.callable != tree.callable ||
        tree.callable !=
        (form == ClassPrintForm::kExpression ? CallableKind::kBaseConstructor : CallableKind::kDerivedConstructor))
      return Failure::kUnsupportedContext;
    if (!parent) {
      if (tree.constructor->Family().root_capture) return Failure::kUnsupportedContext;
      for (const auto& member : tree.members) if (member.Family().root_capture) return Failure::kUnsupportedContext;
      for (const auto& site : tree.definitions)
        for (const auto& member : site.bodies)
          if (member && member->Family().root_capture) return Failure::kUnsupportedContext;
    }
    PrintedClassTree result;
    auto count = tree.members.size() + 1;
    for (const auto& accessor : tree.definitions) {
      if (!budget.work) return Failure::kWorkLimit;
      --budget.work; count += accessor.bodies[0].has_value() + accessor.bodies[1].has_value();
    }
    if (count > budget.bytes / sizeof(PrintedClassTree::Member)) return Failure::kResourceLimit;
    budget.bytes -= count * sizeof(PrintedClassTree::Member); result.members.reserve(count);
    const auto excess = result.members.capacity() - count;
    if (excess > budget.bytes / sizeof(PrintedClassTree::Member)) return Failure::kResourceLimit;
    budget.bytes -= excess * sizeof(PrintedClassTree::Member);
    const auto append = [&](std::string_view text) {
      if (text.size() > limit.bytes - result.bytes) throw Failure::kOutputLimit;
      if (text.size() > budget.work) throw Failure::kWorkLimit;
      budget.work -= text.size(); output << text;
      if (!output) throw Failure::kWrite;
      result.bytes += text.size();
    };
    const auto name = [&] {
      if (tree.name.size() > budget.work) throw Failure::kWorkLimit;
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
    const FamilyDestination destination{Strictness::kStrict, globals, FunctionFragment::kClassMember, {}, nullptr, parent};
    const auto member = [&](const PreparedFamily& family) {
      auto printed = family.PrintDraft(destination, output, {limit.bytes - result.bytes}, budget, indent + 1);
      if (const auto* failure = std::get_if<FamilyPrepareFailure>(&printed)) throw *failure;
      if (const auto* failure = std::get_if<TreePrintError>(&printed)) throw *failure;
      auto positions = std::get<PrintedFamilyDraft>(std::move(printed));
      for (const auto& body : positions.members) result.incomplete |= body.incomplete;
      if (positions.allocated_bytes > budget.bytes) throw Failure::kResourceLimit;
      if (positions.work > budget.work) throw Failure::kWorkLimit;
      budget.bytes -= positions.allocated_bytes; budget.work -= positions.work;
      const auto offset = result.bytes; result.bytes += positions.bytes;
      result.members.push_back({offset, std::move(positions)});
    };
    // Computed-property NamedEvaluation preserves runtime class.name without
    // inventing an immutable class-name binding that shadows a mutable module
    // export read inside a method. The wrapper is never exposed to user code.
    if (form == ClassPrintForm::kExpression) { append("({["); name(); append("]: class {\n"); }
    else append(" {\n");
    member(*tree.constructor);
    for (std::size_t i = 0; i < tree.members.size(); ++i) {
      const auto& family = tree.members[i]; const auto* site = family.ClassCreation();
      if (!site || family.Identity() != tree.identity || site->creator != tree.creation.creator ||
          site->pc != tree.creation.pc || site->property != i) throw Failure::kUnsupportedContext;
      member(family);
    }
    std::optional<analysis::OpId> previous_definition;
    for (const auto& accessor : tree.definitions) {
      if (tree.initializer || (previous_definition && *previous_definition >= accessor.site.definition))
        throw Failure::kUnsupportedContext;
      previous_definition = accessor.site.definition;
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
        if (accessor.bodies[i].has_value() != methods[i].operation.has_value()) throw Failure::kUnsupportedContext;
        if (accessor.bodies[i]) emit_member(*accessor.bodies[i], methods[i].kind);
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
      if (checked->allocated_bytes > budget.bytes) throw Failure::kResourceLimit;
      if (checked->work > budget.work) throw Failure::kWorkLimit;
      budget.bytes -= checked->allocated_bytes; budget.work -= checked->work;
      auto printed = PrintStaticFields(initializer.tree, output, {limit.bytes - result.bytes}, budget, indent + 1, globals, initializer.record);
      if (const auto* error = std::get_if<TreePrintError>(&printed)) throw *error;
      auto positions = std::get<PrintedFunction>(std::move(printed));
      if (positions.allocated_bytes > budget.bytes) throw Failure::kResourceLimit;
      if (positions.work > budget.work) throw Failure::kWorkLimit;
      budget.bytes -= positions.allocated_bytes; budget.work -= positions.work;
      positions.enclosing_offset = result.bytes; result.bytes += positions.bytes;
      result.initializer = std::move(positions);
    }
    for (std::size_t i = 0; i < indent; ++i) append("  ");
    append("}");
    if (form == ClassPrintForm::kExpression) { append("})["); name(); append("]"); }
    result.allocated_bytes = initial.bytes - budget.bytes;
    result.work = initial.work - budget.work;
    return result;
  } catch (Failure failure) { return failure; }
    catch (const FamilyPrepareFailure& failure) { return failure; }
    catch (const std::bad_alloc&) { return Failure::kResourceLimit; }
    catch (const std::length_error&) { return Failure::kResourceLimit; }
    catch (const std::ios_base::failure&) { return Failure::kWrite; }
}
}  // namespace
std::variant<PrintedClassTree, TreePrintError, FamilyPrepareFailure> PrintClassTree(
    const ClassTree& tree, std::ostream& output, EmitBudget limit,
    analysis::SemanticBudget budget, const GlobalScope* globals, std::size_t indent) {
  return PrintClassPart(tree, output, limit, budget, globals, indent, ClassPrintForm::kExpression);
}
}  // namespace abcd::source
