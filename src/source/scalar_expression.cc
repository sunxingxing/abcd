#include "abcd/source/scalar_expression.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <limits>
#include <optional>
#include <ostream>

namespace abcd::source {
namespace {
using namespace analysis;

bool Identifier(std::string_view name) {
  const auto start = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           c == '$';
  };
  if (name.empty() || !start(name.front())) return false;
  for (const char c : name)
    if (!start(c) && !(c >= '0' && c <= '9')) return false;
  // Lexical output boundary, not semantic dispatch. Conservatively exclude
  // strict/module reserved words and restricted binding identifiers.
  constexpr std::string_view reserved[]{
      "arguments", "await",      "break",     "case",    "catch",      "class",
      "const",     "continue",   "debugger",  "default", "delete",     "do",
      "else",      "enum",       "eval",      "export",  "extends",    "false",
      "finally",   "for",        "function",  "if",      "implements", "import",
      "in",        "instanceof", "interface", "let",     "new",        "null",
      "package",   "private",    "protected", "public",  "return",     "static",
      "super",     "switch",     "this",      "throw",   "true",       "try",
      "typeof",    "var",        "void",      "while",   "with",       "yield"};
  static_assert(std::is_sorted(std::begin(reserved), std::end(reserved)));
  return !std::binary_search(std::begin(reserved), std::end(reserved), name);
}

std::optional<std::string_view> BinaryToken(BinaryOperator op) {
  switch (op) {
    case BinaryOperator::kIn:
      return "in";
    case BinaryOperator::kAdd:
      return "+";
    case BinaryOperator::kSubtract:
      return "-";
    case BinaryOperator::kMultiply:
      return "*";
    case BinaryOperator::kDivide:
      return "/";
    case BinaryOperator::kRemainder:
      return "%";
    case BinaryOperator::kExponent:
      return "**";
    case BinaryOperator::kEqual:
      return "==";
    case BinaryOperator::kNotEqual:
      return "!=";
    case BinaryOperator::kLess:
      return "<";
    case BinaryOperator::kLessEqual:
      return "<=";
    case BinaryOperator::kGreater:
      return ">";
    case BinaryOperator::kGreaterEqual:
      return ">=";
    case BinaryOperator::kShiftLeft:
      return "<<";
    case BinaryOperator::kShiftRightUnsigned:
      return ">>>";
    case BinaryOperator::kShiftRightSigned:
      return ">>";
    case BinaryOperator::kBitAnd:
      return "&";
    case BinaryOperator::kBitOr:
      return "|";
    case BinaryOperator::kBitXor:
      return "^";
    case BinaryOperator::kStrictEqual:
      return "===";
    case BinaryOperator::kStrictNotEqual:
      return "!==";
  }
  return std::nullopt;
}
std::optional<std::string_view> UnaryToken(UnaryOperator op) {
  switch (op) {
    case UnaryOperator::kNegate:
      return "-";
    case UnaryOperator::kBitNot:
      return "~";
    case UnaryOperator::kToBoolean:
      return "!!";
    case UnaryOperator::kLogicalNot:
      return "!";
    case UnaryOperator::kTypeOf:
      return "typeof ";
    case UnaryOperator::kToNumber:
      return "+";
    case UnaryOperator::kFalseOrZero:
    case UnaryOperator::kTrueOrNonzeroNumber:
    case UnaryOperator::kIncrement:
    case UnaryOperator::kDecrement:
    case UnaryOperator::kToNumeric:
      return std::nullopt;
  }
  return std::nullopt;
}

class ExpressionWriter {
 public:
  ExpressionWriter(ScalarNames names, EmitBudget budget, std::ostream* output)
      : names_(names), limit_(budget.bytes), output_(output) {}
  std::variant<std::size_t, EmitError> Write(
      const ScalarExpression& expression) {
    std::visit([&](const auto& node) { Expression(node); }, expression);
    if (error_) return *error_;
    return bytes_;
  }

 private:
  void Append(std::string_view token) {
    if (error_) return;
    if (token.size() > limit_ - bytes_) {
      error_ = EmitError::kOutputLimit;
      return;
    }
    bytes_ += token.size();
    if (output_) {
      output_->write(token.data(), static_cast<std::streamsize>(token.size()));
      if (!*output_) error_ = EmitError::kWrite;
    }
  }
  void Name(std::string_view name) {
    if (!Identifier(name)) {
      error_ = EmitError::kInvalidBinding;
      return;
    }
    Append(name);
  }
  template <class Number>
  void NumberToken(Number number) {
    constexpr std::size_t kNumberTokenBytes = 64;
    std::array<char, kNumberTokenBytes> buffer{};
    const auto converted =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), number);
    if (converted.ec != std::errc{}) {
      error_ = EmitError::kInvalidExpression;
      return;
    }
    // Parenthesize all numbers, including negative bases before exponentiation.
    Append("(");
    Append(std::string_view(buffer.data(), converted.ptr - buffer.data()));
    Append(")");
  }
  void Value(Accumulator) { Name(names_.accumulator); }
  void Value(abc::RegisterId reg) {
    if (reg.value >= names_.registers.size()) {
      error_ = EmitError::kInvalidBinding;
      return;
    }
    Name(names_.registers[reg.value]);
  }
  void Value(IntegerLiteral integer) { NumberToken(integer.value); }
  void Value(Float64Bits number) {
    static_assert(sizeof(double) == sizeof(std::uint64_t) &&
                  std::numeric_limits<double>::is_iec559);
    constexpr std::uint64_t kExponentMask = UINT64_C(0x7ff0000000000000);
    constexpr std::uint64_t kFractionMask = UINT64_C(0x000fffffffffffff);
    constexpr std::uint64_t kSignMask = UINT64_C(0x8000000000000000);
    if ((number.value & kExponentMask) == kExponentMask) {
      if ((number.value & kFractionMask) != 0) {
        error_ = EmitError::kNaNPayload;
        return;
      }
      Append((number.value & kSignMask) != 0 ? "(-1 / 0)" : "(1 / 0)");
      return;
    }
    if (number.value == kSignMask) {
      Append("(-0)");
      return;
    }
    NumberToken(std::bit_cast<double>(number.value));
  }
  void Value(EmptyArray) { Append("[]"); }
  void Value(SpecialValue value) {
    switch (value) {
      case SpecialValue::kUndefined:
        Append("(void 0)");
        return;
      case SpecialValue::kNull:
        Append("null");
        return;
      case SpecialValue::kTrue:
        Append("true");
        return;
      case SpecialValue::kFalse:
        Append("false");
        return;
      case SpecialValue::kNaN:
        Append("(0 / 0)");
        return;
      case SpecialValue::kInfinity:
        Append("(1 / 0)");
        return;
    }
    error_ = EmitError::kInvalidExpression;
  }
  void Expression(const ScalarValue& value) {
    if (!error_) std::visit([&](const auto& atom) { Value(atom); }, value);
  }
  void Expression(const UnaryExpression& unary) {
    if (unary.op == UnaryOperator::kToNumeric) {
      // Slots/literals have no read effects. Preserve existing numeric values
      // without arithmetic; otherwise the inner negation performs ToNumeric
      // once and the outer negation restores its numeric value (also BigInt
      // and signed zero). Do not replace this with unary + or duplicated
      // coercion of an object. No mutable global numeric helper is required.
      Append("((typeof ");
      Expression(unary.input);
      Append(" === \"number\" || typeof ");
      Expression(unary.input);
      Append(" === \"bigint\") ? ");
      Expression(unary.input);
      Append(" : (-(-");
      Expression(unary.input);
      Append(")))");
      return;
    }
    if (unary.op == UnaryOperator::kIncrement ||
        unary.op == UnaryOperator::kDecrement) {
      // Ark updates acc, not the source register. A private parameter keeps
      // ToNumeric/BigInt semantics without mutating the input slot or exposing
      // a partial destination on coercion failure. The argument is outside
      // the parameter scope, so a caller binding named value is safe too.
      Append(unary.op == UnaryOperator::kIncrement ? "((value) => ++value)("
                                                   : "((value) => --value)(");
      Expression(unary.input);
      Append(")");
      return;
    }
    // Inputs are side-effect-free values/local slots, never arbitrary source
    // expressions. Repeated references here must not duplicate a computation.
    if (unary.op == UnaryOperator::kFalseOrZero) {
      Append("(");
      Expression(unary.input);
      Append(" === false || ");
      Expression(unary.input);
      Append(" === 0)");
      return;
    }
    if (unary.op == UnaryOperator::kTrueOrNonzeroNumber) {
      Append("(");
      Expression(unary.input);
      Append(" === true || (typeof ");
      Expression(unary.input);
      Append(" === \"number\" && ");
      Expression(unary.input);
      Append(" !== 0))");
      return;
    }
    const auto token = UnaryToken(unary.op);
    if (!token) {
      error_ = EmitError::kInvalidExpression;
      return;
    }
    Append("(");
    Append(*token);
    Expression(unary.input);
    Append(")");
  }
  void Expression(const BinaryExpression& binary) {
    const auto token = BinaryToken(binary.op);
    if (!token) {
      error_ = EmitError::kInvalidExpression;
      return;
    }
    Append("(");
    Expression(binary.left);
    Append(" ");
    Append(*token);
    Append(" ");
    Expression(binary.right);
    Append(")");
  }
  void Expression(const PropertyRead& property) {
    Append("(");
    Expression(property.receiver);
    Append(")[");
    Expression(property.key);
    Append("]");
  }
  ScalarNames names_;
  std::size_t limit_;
  std::ostream* output_;
  std::size_t bytes_ = 0;
  std::optional<EmitError> error_;
};
}  // namespace
bool IsSourceIdentifier(std::string_view name) { return Identifier(name); }

std::variant<std::size_t, EmitError> WriteScalarExpression(
    const ScalarExpression& expression, ScalarNames names, std::ostream& output,
    EmitBudget budget) {
  // Same pure rendering path for preflight and output: invalid late operands
  // or exhausted budgets cannot leak an earlier prefix into generated files.
  const auto checked =
      ExpressionWriter(names, budget, nullptr).Write(expression);
  if (std::holds_alternative<EmitError>(checked)) return checked;
  try {
    return ExpressionWriter(names, budget, &output).Write(expression);
  } catch (const std::ios_base::failure&) {
    return EmitError::kWrite;
  }
}
}  // namespace abcd::source
