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
using PrintAction = std::variant<std::string_view, ExprId, EndExpression,
    Statement, EndStatement, BlockCursor, Indent, PropertyKey, SymbolName, ArgumentCursor>;
struct LocatedAnchor { BlockBoundary boundary; AnchorId id; };

class TreePrinter {
 public:
  TreePrinter(const VerifiedFunction& function, std::ostream& output,
              EmitBudget limit, analysis::SemanticBudget scratch)
      : function_(function), tree_(function.Tree()), output_(output),
        limit_(limit.bytes), initial_(scratch), remaining_(scratch) {}
  PrintedFunction Print() {
    if (tree_.callable != CallableKind::kMethod ||
        tree_.strictness != Strictness::kStrict || tree_.self)
      throw Error::kUnsupportedContext;
    result_.identity = tree_.identity;
    result_.method = tree_.method;
    result_.value_revision = tree_.value_revision;
    result_.cfg_revision = tree_.cfg_revision;
    Array(result_.parameters, tree_.parameters.size());
    Array(result_.expressions, tree_.expressions.size());
    Array(result_.statements, tree_.statements.size());
    Array(result_.anchors, tree_.anchors.size());
    PrepareAnchors();
    switch (function_.Creation().placement) {
      case abc::ClassPlacement::kStatic: Append("static "); break;
      case abc::ClassPlacement::kPrototype: break;
      default: throw Error::kUnsupportedContext;
    }
    Key(function_.Name());
    Append("(");
    for (std::size_t i = 0; i < tree_.parameters.size(); ++i) {
      Step();
      const auto& parameter = tree_.parameters[i];
      if (parameter.rest) throw Error::kUnsupportedContext;
      if (i != 0) Append(", ");
      result_.parameters[i].begin = result_.bytes;
      Name(parameter.symbol);
      if (parameter.initializer) {
        if (parameter.initializer->value >= tree_.expressions.size() ||
            !std::holds_alternative<TreeLiteral>(tree_.expressions[parameter.initializer->value].expression))
          throw Error::kUnsupportedContext;
        Append(" = ");
        Push(*parameter.initializer);
        Drain();
      }
      result_.parameters[i].end = result_.bytes;
    }
    Append(") ");
    Push(Statement{tree_.body, 0});
    Drain();
    for (std::size_t i = 0; i < tree_.anchors.size(); ++i) {
      Step();
      if (std::holds_alternative<FunctionExit>(tree_.anchors[i].position))
        result_.anchors[i] = result_.bytes;
    }
    Append("\n");
    result_.allocated_bytes = initial_.bytes - remaining_.bytes;
    result_.work = initial_.work - remaining_.work;
    return std::move(result_);
  }
 private:
  void Drain() {
    while (!actions_.empty()) {
      Step();
      const auto action = actions_.back();
      actions_.pop_back();
      std::visit([&](const auto& value) { Run(value); }, action);
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
      Admit(capacity, sizeof(PrintAction));
      actions_.reserve(capacity);
      Admit(actions_.capacity() - capacity, sizeof(PrintAction));
    }
    actions_.push_back(action);
  }
  void Append(std::string_view text) {
    Step(text.size());
    if (text.size() > limit_ - result_.bytes) throw Error::kOutputLimit;
    output_.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output_) throw Error::kWrite;
    result_.bytes += text.size();
  }
  void Quoted(std::u16string_view text) {
    Step(text.size());
    const auto written = WriteStringLiteral(text, output_, {limit_ - result_.bytes});
    if (const auto* bytes = std::get_if<std::size_t>(&written)) result_.bytes += *bytes;
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
  void Name(SymbolId id) { Identifier(tree_.names[tree_.symbols[id.value].name.value]); }
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
  void Literal(TextId id) { Quoted(tree_.texts[id.value]); }
  void Expression(const TreeLiteral& node) {
    std::visit([&](auto value) { Literal(value); }, node);
  }
  void Expression(SymbolExpr node) { Name(node.symbol); }
  void Expression(GlobalNameExpr node) { Identifier(tree_.names[node.name.value]); }
  void Expression(ThisExpr) { Append("this"); }
  void Member(ExprId base, const MemberKey& key) {
    if (const auto* name = std::get_if<NameId>(&key)) Push(PropertyKey{*name});
    else {
      Push(std::string_view("]"));
      Push(std::get<ComputedKey>(key).expression);
      Push(std::string_view("["));
    }
    const auto& expression = tree_.expressions[base.value].expression;
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
  void Run(ArgumentCursor cursor) {
    if (cursor.index == cursor.range.count) return;
    if (cursor.index != 0) Append(", ");
    Push(ArgumentCursor{cursor.range, cursor.index + 1});
    Push(tree_.arguments[cursor.range.first + cursor.index]);
  }
  void Run(std::string_view token) { Append(token); }
  void Run(SymbolName symbol) { Name(symbol.id); }
  void Run(PropertyKey key) {
    const auto& name = tree_.names[key.id.value];
    Step(name.size());
    if (IsSourceIdentifier(name)) { Append("."); Identifier(name); }
    else { Append("["); Quoted(name); Append("]"); }
  }
  void Run(ExprId id) {
    result_.expressions[id.value].begin = result_.bytes;
    Push(EndExpression{id});
    std::visit([&](const auto& node) { Expression(node); }, tree_.expressions[id.value].expression);
  }
  void Run(EndExpression end) { result_.expressions[end.id.value].end = result_.bytes; }
  void Run(Indent indent) {
    constexpr std::string_view kIndent = "  ";
    for (std::size_t i = 0; i < indent.depth; ++i) Append(kIndent);
  }
  void StatementNode(const BlockStmt&, Statement at) {
    Append("{\n"); Push(BlockCursor{at.id, 0, at.depth});
  }
  void StatementNode(const DeclarationStmt& node, Statement) {
    switch (tree_.symbols[node.symbol.value].declaration) {
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
  void Run(Statement at) {
    result_.statements[at.id.value].begin = result_.bytes;
    Push(EndStatement{at.id});
    std::visit([&](const auto& node) { StatementNode(node, at); }, tree_.statements[at.id.value].statement);
  }
  void Run(EndStatement end) { result_.statements[end.id.value].end = result_.bytes; }
  void Run(BlockCursor at) {
    const auto& block = std::get<BlockStmt>(tree_.statements[at.id.value].statement);
    Boundary({at.id, at.index});
    if (at.index == block.child_count) { Run(Indent{at.depth}); Append("}"); return; }
    Push(BlockCursor{at.id, at.index + 1, at.depth});
    Push(std::string_view("\n"));
    Push(Statement{tree_.children[block.first_child + at.index], at.depth + 1});
    Push(Indent{at.depth + 1});
  }
  void PrepareAnchors() {
    Array(located_, tree_.anchors.size());
    std::size_t count = 0;
    for (std::size_t i = 0; i < tree_.anchors.size(); ++i) {
      Step();
      if (const auto* position = std::get_if<BlockBoundary>(&tree_.anchors[i].position))
        located_[count++] = {*position, {static_cast<std::uint32_t>(i)}};
    }
    located_.resize(count);
    std::sort(located_.begin(), located_.end(), [&](const auto& left, const auto& right) {
      Step(); return Less(left.boundary, right.boundary);
    });
  }
  static bool Less(BlockBoundary left, BlockBoundary right) {
    return left.block != right.block ? left.block < right.block : left.index < right.index;
  }
  void Boundary(BlockBoundary position) {
    // Binary lookup per structural boundary, not a scan of every method anchor.
    auto found = std::lower_bound(located_.begin(), located_.end(), position,
        [&](const auto& item, auto key) { Step(); return Less(item.boundary, key); });
    while (found != located_.end() && !Less(position, found->boundary)) {
      Step(); result_.anchors[found->id.value] = result_.bytes; ++found;
    }
  }
  const VerifiedFunction& function_;
  const FunctionTree& tree_;
  std::ostream& output_;
  std::size_t limit_;
  analysis::SemanticBudget initial_;
  analysis::SemanticBudget remaining_;
  PrintedFunction result_;
  std::vector<PrintAction> actions_;
  std::vector<LocatedAnchor> located_;
};
}  // namespace

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
