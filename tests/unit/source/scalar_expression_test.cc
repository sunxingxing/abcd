#include "abcd/source/scalar_expression.h"

#include <array>
#include <cstdlib>
#include <locale>
#include <sstream>

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
class FailingBuffer : public std::streambuf {
  std::streamsize xsputn(const char*, std::streamsize) override { return 0; }
};
class DecimalComma : public std::numpunct<char> {
  char do_decimal_point() const override { return ','; }
};
}  // namespace
int main() {
  using namespace abcd::analysis;
  using namespace abcd::source;
  const std::array<std::string_view, 1> registers{"left"};
  const ScalarNames names{"right", registers};
  for (const auto predicate :
       {UnaryOperator::kFalseOrZero, UnaryOperator::kTrueOrNonzeroNumber}) {
    const ScalarExpression branch = UnaryExpression{predicate, Accumulator{}};
    std::ostringstream full;
    Check(std::holds_alternative<std::size_t>(
        WriteScalarExpression(branch, names, full)));
    std::ostringstream exact;
    Check(std::get<std::size_t>(WriteScalarExpression(
              branch, names, exact, {full.str().size()})) == full.str().size());
    Check(exact.str() == full.str());
    std::ostringstream short_output;
    Check(std::get<EmitError>(WriteScalarExpression(branch, names, short_output,
                                                    {full.str().size() - 1})) ==
          EmitError::kOutputLimit);
    Check(short_output.str().empty());
    Check(std::get<EmitError>(
              WriteScalarExpression(branch, {"bad.name", {}}, short_output)) ==
          EmitError::kInvalidBinding);
    Check(short_output.str().empty());
  }
  const ScalarExpression expression = BinaryExpression{
      BinaryOperator::kSubtract, abcd::abc::RegisterId{0}, Accumulator{}};
  std::ostringstream output;
  auto result = WriteScalarExpression(expression, names, output);
  Check(std::get<std::size_t>(result) == output.str().size());
  Check(output.str() == "(left - right)");
  std::ostringstream bounded;
  Check(std::get<EmitError>(WriteScalarExpression(
            expression, names, bounded, {1})) == EmitError::kOutputLimit);
  Check(bounded.str().empty());
  for (const std::string_view name :
       {"", "class", "await", "eval", "arguments", "1name", "a.b", "a b"}) {
    Check(std::get<EmitError>(
              WriteScalarExpression(expression, {name, registers}, bounded)) ==
          EmitError::kInvalidBinding);
    Check(bounded.str().empty());
  }
  for (const std::size_t size : {200U, 201U, 4096U, 65536U}) {
    const std::string name(size, 'x');
    std::ostringstream exact;
    const auto first = WriteScalarExpression(ScalarValue{Accumulator{}},
                                             {name, {}}, exact, {size});
    Check(std::get<std::size_t>(first) == size && exact.str() == name);
    std::ostringstream short_output;
    Check(std::get<EmitError>(WriteScalarExpression(
              ScalarValue{Accumulator{}}, {name, {}}, short_output,
              {size - 1})) == EmitError::kOutputLimit);
    Check(short_output.str().empty());
  }
  FailingBuffer failed_buffer;
  std::ostream failed(&failed_buffer);
  Check(std::get<EmitError>(WriteScalarExpression(expression, names, failed)) ==
        EmitError::kWrite);
  std::ostream throwing(&failed_buffer);
  throwing.exceptions(std::ios::badbit | std::ios::failbit);
  Check(std::get<EmitError>(WriteScalarExpression(
            expression, names, throwing)) == EmitError::kWrite);
  std::ostringstream localized;
  localized.imbue(std::locale(std::locale::classic(), new DecimalComma));
  Check(std::holds_alternative<std::size_t>(WriteScalarExpression(
      ScalarValue{Float64Bits{UINT64_C(0x3ff8000000000000)}}, {}, localized)));
  Check(localized.str() == "(1.5)");
  Check(std::get<EmitError>(WriteScalarExpression(
            BinaryExpression{static_cast<BinaryOperator>(UINT8_MAX),
                             Accumulator{}, Accumulator{}},
            names, bounded)) == EmitError::kInvalidExpression);
  const std::array<std::string_view, 1> invalid{"x); sideEffect(); (x"};
  Check(std::get<EmitError>(
            WriteScalarExpression(expression, {"right", invalid}, bounded)) ==
        EmitError::kInvalidBinding);
  Check(bounded.str().empty());
  const ScalarExpression negative_zero =
      ScalarValue{Float64Bits{UINT64_C(0x8000000000000000)}};
  std::ostringstream zero;
  Check(std::holds_alternative<std::size_t>(
      WriteScalarExpression(negative_zero, {}, zero)));
  Check(zero.str() == "(-0)");
  const ScalarExpression nan_payload =
      ScalarValue{Float64Bits{UINT64_C(0x7ff8000000000042)}};
  Check(std::get<EmitError>(WriteScalarExpression(nan_payload, {}, bounded)) ==
        EmitError::kNaNPayload);
  Check(bounded.str().empty());
}
