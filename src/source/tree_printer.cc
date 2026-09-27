#include "abcd/source/tree_printer.h"

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
#include "tree_checks.h"

namespace abcd::source {
namespace {
using Error = TreePrintError;
struct EndExpression { ExprId id; };
struct Statement { StmtId id; std::size_t depth; };
struct EndStatement { StmtId id; };
struct BlockCursor { StmtId id; std::uint32_t index; std::size_t depth; };
struct Indent { std::size_t depth; };
struct PropertyKey { NameId id; };
struct SymbolName { SymbolId id; };
struct ArgumentCursor { ArgumentRange range; std::uint32_t index; };
struct ObjectCursor { PropertyRange range; std::uint32_t index; };
struct EndProperty { PropertyId id; };
struct BeginFunction {};
struct EndFunction {};
struct ParameterCursor { std::uint32_t index; };
struct EndParameter { std::uint32_t index; };
using PrintAction = std::variant<std::string_view, ExprId, EndExpression,
    Statement, EndStatement, BlockCursor, Indent, PropertyKey, SymbolName, ArgumentCursor,
    ObjectCursor, EndProperty, BeginFunction, EndFunction, ParameterCursor, EndParameter>;
struct OwnedAction { std::size_t owner; std::size_t depth; PrintAction action; };
struct LocatedAnchor { BlockBoundary boundary; AnchorId id; };
struct OwnerTree { FunctionOwnerId owner; std::size_t member; };

class TreePrinter {
 public:
  TreePrinter(const VerifiedFunction& function, std::ostream& output,
              EmitBudget limit, analysis::SemanticBudget scratch)
      : function_(&function), tree_(&function.Tree()), output_(output),
        limit_(limit.bytes), initial_(scratch), remaining_(scratch) {}
  TreePrinter(const ConstructedFamily& family, std::ostream& output,
              EmitBudget limit, analysis::SemanticBudget scratch)
      : family_(&family), output_(output), limit_(limit.bytes), initial_(scratch), remaining_(scratch) {}
  PrintedFunction Print() {
    if (!function_->ClassCreation() || Tree().callable != CallableKind::kMethod ||
        Tree().strictness != Strictness::kStrict || Tree().self || Tree().capture_scope || !Tree().captures.empty() ||
        Tree().module_scope || !Tree().modules.empty())
      throw Error::kUnsupportedContext;
    // Standalone printing has no verified family/child lookup. Reject before
    // writing the header, never leave a partial fake function body behind.
    for (const auto& expression : Tree().expressions) {
      Step();
      if (std::holds_alternative<FunctionExpr>(expression.expression)) throw Error::kUnsupportedContext;
    }
    Result().identity = Tree().identity;
    Result().method = Tree().method;
    Result().value_revision = Tree().value_revision;
    Result().cfg_revision = Tree().cfg_revision;
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
      if (parameter.rest) throw Error::kUnsupportedContext;
      if (i != 0) Append(", ");
      Result().parameters[i].begin = written_;
      Name(parameter.symbol);
      if (parameter.initializer) {
        if (parameter.initializer->value >= Tree().expressions.size() ||
            !std::holds_alternative<TreeLiteral>(Tree().expressions[parameter.initializer->value].expression))
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
  PrintedFamilyDraft Draft() {
    if (family_->root.value >= family_->members.size()) throw Error::kUnsupportedContext;
    Array(draft_.members, family_->members.size()); Array(draft_.functions, family_->members.size());
    Array(locations_, family_->members.size()); Array(owners_, family_->members.size());
    Array(printed_, family_->members.size());
    for (std::size_t i = 0; i < family_->members.size(); ++i) {
      current_ = i; Step();
      const auto checked = VerifyTreeStructure(Tree(), remaining_);
      const auto* structure = std::get_if<TreeStructure>(&checked);
      if (!structure) {
        switch (std::get<TreeStructureError>(checked)) {
          case TreeStructureError::kResourceLimit: throw Error::kResourceLimit;
          case TreeStructureError::kWorkLimit: throw Error::kWorkLimit;
          default: throw Error::kUnsupportedContext;
        }
      }
      Admit(structure->allocated_bytes, 1); Step(structure->work);
      if (Tree().self || Tree().strictness != Strictness::kStrict ||
          (Tree().callable != CallableKind::kFunction && Tree().callable != CallableKind::kArrow &&
           Tree().callable != CallableKind::kMethod)) throw Error::kUnsupportedContext;
      // A class method cannot be silently changed into an ordinary function.
      // Root class fragments remain handled by the verified member entry.
      if (Tree().callable == CallableKind::kMethod) throw Error::kUnsupportedContext;
      // Naming adds a fresh, unobservable wrapper object. Its allocation-failure
      // timing is admitted only by the already-versioned resource-failure scope.
      if (Tree().inferred_name && ResourceFailures(Tree().identity.contract) != ResourceFailureScope::kExcluded)
        throw Error::kUnsupportedContext;
      owners_[i] = {Tree().owner, i};
      Result().identity = Tree().identity; Result().method = Tree().method;
      Result().value_revision = Tree().value_revision; Result().cfg_revision = Tree().cfg_revision;
      Result().fragment = FunctionFragment::kConditionalExpression;
      Array(Result().parameters, Tree().parameters.size()); Array(Result().expressions, Tree().expressions.size());
      Array(Result().properties, Tree().properties.size()); Array(Result().statements, Tree().statements.size());
      Array(Result().anchors, Tree().anchors.size()); PrepareAnchors();
    }
    std::sort(owners_.begin(), owners_.end(), [&](const auto& a, const auto& b) { Step(); return a.owner < b.owner; });
    for (std::size_t i = 1; i < owners_.size(); ++i) {
      Step(); if (owners_[i - 1].owner == owners_[i].owner) throw Error::kUnsupportedContext;
    }
    current_ = family_->root.value;
    Push(BeginFunction{}); Drain(); Append("\n");
    for (const auto emitted : printed_) { Step(); if (!emitted) throw Error::kUnsupportedContext; }
    draft_.bytes = written_; draft_.allocated_bytes = initial_.bytes - remaining_.bytes;
    draft_.work = initial_.work - remaining_.work;
    return std::move(draft_);
  }
 private:
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
  void Expression(ThisExpr) { Append("this"); }
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
  void Expression(UnaryExpr node) {
    Append("("); Append(TreeUnaryToken(node.operation));
    // Lifted typeof consumes an already obtained value. JS typeof Identifier
    // suppresses an unresolvable-name error, unlike TRYLDGLOBALBYNAME followed
    // by TYPEOF. Force GetValue without evaluating the operand twice.
    if (node.operation == TreeUnary::kTypeof) {
      Append("(0, "); Push(std::string_view("))"));
    } else Push(std::string_view(")"));
    Push(node.operand);
  }
  void Expression(BinaryExpr node) {
    Append("(");
    Push(std::string_view(")")); Push(node.right);
    Push(TreeBinaryToken(node.operation));
    // Unary/negative literal printers already parenthesize their complete
    // expression, including when used as exponentiation's left operand.
    Push(node.left);
  }
  void Expression(LogicalExpr node) {
    Append("("); Push(std::string_view(")")); Push(node.right);
    Push(TreeLogicalToken(node.operation)); Push(node.left);
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
    Push(node.callee);
  }
  void Expression(const ConstructExpr& node) {
    Append("(new (");
    Push(std::string_view("))"));
    Push(ArgumentCursor{node.arguments, 0});
    Push(std::string_view(")("));
    Push(node.constructor);
  }
  void Expression(EmptyArrayExpr) { Append("[]"); }
  void Expression(EmptyObjectExpr) { Append("({})"); }
  void Expression(FunctionExpr node) {
    if (!family_) throw Error::kUnsupportedContext;
    const auto child = std::lower_bound(owners_.begin(), owners_.end(), node.child,
        [&](const auto& item, FunctionOwnerId owner) { Step(); return item.owner < owner; });
    if (child == owners_.end() || child->owner != node.child) throw Error::kUnsupportedContext;
    const auto parent = current_; current_ = child->member;
    Push(BeginFunction{}); current_ = parent;
  }
  void Run(BeginFunction) {
    if (!family_ || printed_[current_]) throw Error::kUnsupportedContext;
    printed_[current_] = true; draft_.functions[current_].begin = written_;
    if (Tree().inferred_name) {
      // NamedEvaluation, not a new lexical self scope. Computed keys preserve
      // even an empty name or __proto__, without Object.defineProperty calls.
      Append("({["); Quoted(Tree().names[Tree().inferred_name->value]); Append("]: ");
      Append(Tree().callable == CallableKind::kArrow ? "(" : "function(");
    } else Append(Tree().callable == CallableKind::kArrow ? "((" : "(function(");
    Push(EndFunction{}); Push(Statement{Tree().body, depth_});
    Push(std::string_view(Tree().callable == CallableKind::kArrow ? ") => " : ") "));
    Push(ParameterCursor{0});
  }
  void Run(EndFunction) {
    for (std::size_t i = 0; i < Tree().anchors.size(); ++i) {
      Step(); if (std::holds_alternative<FunctionExit>(Tree().anchors[i].position)) Result().anchors[i] = written_;
    }
    if (Tree().inferred_name) {
      Append("})["); Quoted(Tree().names[Tree().inferred_name->value]); Append("]");
    } else Append(")");
    draft_.functions[current_].end = written_;
    Result().bytes = written_ - draft_.functions[current_].begin;
  }
  void Run(ParameterCursor cursor) {
    if (cursor.index == Tree().parameters.size()) return;
    if (cursor.index) Append(", ");
    const auto& parameter = Tree().parameters[cursor.index];
    if (parameter.rest) throw Error::kUnsupportedContext;
    Result().parameters[cursor.index].begin = written_; Name(parameter.symbol);
    Push(ParameterCursor{cursor.index + 1}); Push(EndParameter{cursor.index});
    if (parameter.initializer) {
      if (!std::holds_alternative<TreeLiteral>(Tree().expressions[parameter.initializer->value].expression))
        throw Error::kUnsupportedContext;
      Append(" = "); Push(*parameter.initializer);
    }
  }
  void Run(EndParameter end) { Result().parameters[end.index].end = written_; }
  void Expression(ObjectExpr node) {
    Append("({"); Push(std::string_view("})")); Push(ObjectCursor{node.properties, 0});
  }
  void Run(ObjectCursor cursor) {
    if (cursor.index == cursor.range.count) return;
    if (cursor.index != 0) Append(", ");
    const PropertyId id{cursor.range.first + cursor.index};
    const auto& property = Tree().properties[id.value];
    Result().properties[id.value].begin = written_;
    Append("["); Quoted(Tree().names[property.key.value]); Append("]: ");
    Push(ObjectCursor{cursor.range, cursor.index + 1});
    Push(EndProperty{id}); Push(property.value);
  }
  void Run(EndProperty property) { Result().properties[property.id.value].end = written_; }
  void Run(ArgumentCursor cursor) {
    if (cursor.index == cursor.range.count) return;
    if (cursor.index != 0) Append(", ");
    Push(ArgumentCursor{cursor.range, cursor.index + 1});
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
  void Run(ExprId id) {
    Result().expressions[id.value].begin = written_;
    Push(EndExpression{id});
    std::visit([&](const auto& node) { Expression(node); }, Tree().expressions[id.value].expression);
  }
  void Run(EndExpression end) { Result().expressions[end.id.value].end = written_; }
  void Run(Indent indent) {
    constexpr std::string_view kIndent = "  ";
    for (std::size_t i = 0; i < indent.depth; ++i) Append(kIndent);
  }
  void StatementNode(const BlockStmt&, Statement at) {
    Append("{\n"); Push(BlockCursor{at.id, 0, at.depth});
  }
  void StatementNode(const DeclarationStmt& node, Statement) {
    switch (Tree().symbols[node.symbol.value].declaration) {
      case DeclarationKind::kLet: Append("let "); break;
      case DeclarationKind::kConst: Append("const "); break;
      case DeclarationKind::kParameter: throw Error::kUnsupportedContext;
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
    Append("return"); Push(std::string_view(";"));
    if (node.value) { Append(" "); Push(*node.value); }
  }
  void StatementNode(LoopStmt node, Statement at) {
    Append("while (true) ");
    Push(Statement{node.body, at.depth});
  }
  void StatementNode(BreakStmt, Statement) { Append("break;"); }
  void StatementNode(ContinueStmt, Statement) { Append("continue;"); }
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
    if (at.index == block.child_count) { Run(Indent{at.depth}); Append("}"); return; }
    Push(BlockCursor{at.id, at.index + 1, at.depth});
    Push(std::string_view("\n"));
    Push(Statement{Tree().children[block.first_child + at.index], at.depth + 1});
    Push(Indent{at.depth + 1});
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
  std::ostream& output_;
  std::size_t limit_;
  analysis::SemanticBudget initial_;
  analysis::SemanticBudget remaining_;
  PrintedFunction result_;
  PrintedFamilyDraft draft_;
  std::size_t current_ = 0;
  std::size_t depth_ = 0;
  std::size_t written_ = 0;
  std::vector<OwnedAction> actions_;
  std::vector<LocatedAnchor> located_;
  std::vector<std::vector<LocatedAnchor>> locations_;
  std::vector<OwnerTree> owners_;
  std::vector<std::uint8_t> printed_;
};
}  // namespace

std::variant<PrintedFamilyDraft, TreePrintError> PrintFamilyDraft(const ConstructedFamily& family,
    std::ostream& output, EmitBudget limit, analysis::SemanticBudget scratch) {
  try { return TreePrinter(family, output, limit, scratch).Draft();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit;
  } catch (const std::ios_base::failure&) { return Error::kWrite; }
}

std::variant<PrintedFunction, TreePrintError> PrintFunctionTree(
    const VerifiedFunction& function, std::ostream& output,
    EmitBudget output_budget, analysis::SemanticBudget scratch, const GlobalScope* scope) {
  try {
    const auto globals = VerifyTreeGlobals(function.Tree(), scope, scratch);
    const auto* checked = std::get_if<TreeGlobals>(&globals);
    if (!checked) {
      const auto error = std::get<TreeGlobalError>(globals);
      if (error == TreeGlobalError::kResourceLimit) return Error::kResourceLimit;
      if (error == TreeGlobalError::kWorkLimit) return Error::kWorkLimit;
      return Error::kUnsupportedContext;
    }
    scratch.work -= checked->work;
    auto result = TreePrinter(function, output, output_budget, scratch).Print();
    result.work += checked->work;
    result.allocated_bytes = std::max(result.allocated_bytes, checked->allocated_bytes);
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit;
  } catch (const std::ios_base::failure&) { return Error::kWrite; }
}
}  // namespace abcd::source
