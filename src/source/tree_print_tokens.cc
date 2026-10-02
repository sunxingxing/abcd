#include "tree_print_tokens.h"

#include "abcd/source/tree_printer.h"

namespace abcd::source {
std::string_view TreeBinaryToken(analysis::BinaryOperator operation) {
  using Op = analysis::BinaryOperator;
  switch (operation) {
    case Op::kAdd: return " + ";
    case Op::kSubtract: return " - ";
    case Op::kMultiply: return " * ";
    case Op::kDivide: return " / ";
    case Op::kRemainder: return " % ";
    case Op::kExponent: return " ** ";
    case Op::kEqual: return " == ";
    case Op::kNotEqual: return " != ";
    case Op::kLess: return " < ";
    case Op::kLessEqual: return " <= ";
    case Op::kGreater: return " > ";
    case Op::kGreaterEqual: return " >= ";
    case Op::kShiftLeft: return " << ";
    case Op::kShiftRightUnsigned: return " >>> ";
    case Op::kShiftRightSigned: return " >> ";
    case Op::kBitAnd: return " & ";
    case Op::kBitOr: return " | ";
    case Op::kBitXor: return " ^ ";
    case Op::kStrictEqual: return " === ";
    case Op::kStrictNotEqual: return " !== ";
    case Op::kIn: return " in ";
    case Op::kInstanceOf: return " instanceof ";
  }
  throw TreePrintError::kUnsupportedContext;
}
std::string_view TreeUnaryToken(TreeUnary operation) {
  switch (operation) {
    case TreeUnary::kNot: return "!";
    case TreeUnary::kNegate: return "-";
    case TreeUnary::kBitNot: return "~";
    case TreeUnary::kPlus: return "+";
    case TreeUnary::kTypeof: return "typeof ";
  }
  throw TreePrintError::kUnsupportedContext;
}
std::string_view TreeLogicalToken(TreeLogical operation) {
  switch (operation) {
    case TreeLogical::kAnd: return " && ";
    case TreeLogical::kOr: return " || ";
    case TreeLogical::kNullish: return " ?? ";
  }
  throw TreePrintError::kUnsupportedContext;
}
}  // namespace abcd::source
