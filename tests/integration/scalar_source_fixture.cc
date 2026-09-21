#include <array>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

#include "abcd/analysis/branch_condition.h"
#include "abcd/source/scalar_expression.h"

namespace {
using namespace abcd;
void Emit(const analysis::ScalarExpression& expression) {
  const std::array<std::string_view, 1> registers{"left"};
  if (!std::holds_alternative<std::size_t>(source::WriteScalarExpression(
          expression, {"right", registers}, std::cout)))
    std::abort();
}
void Bytecode(std::initializer_list<std::uint8_t> bytes) {
  const std::vector<std::uint8_t> storage(bytes);
  const auto decoded = std::get<abc::DecodedInstruction>(
      abc::DecodeInstruction(storage, abc::ProfileId::kDynamic1206));
  const auto assignment = std::get<analysis::ScalarAssignment>(
      analysis::LiftScalarInstruction(decoded, 1));
  std::cout << "(left,right)=>";
  Emit(assignment.expression);
  std::cout << ",\n";
}
}  // namespace
int main() {
  using namespace abcd::analysis;
  std::cout << "export const branches = [\n";
  for (const std::uint8_t opcode : {0x4f, 0x50, 0x9a, 0x51, 0x9b, 0x9c}) {
    const std::array<std::uint8_t, 5> bytes{opcode, 0, 0, 0, 0};
    const auto decoded =
        std::get<abcd::abc::DecodedInstruction>(abcd::abc::DecodeInstruction(
            bytes, abcd::abc::ProfileId::kDynamic1206));
    std::cout << "(right)=>";
    Emit(std::get<UnaryExpression>(LiftBranchCondition(decoded)));
    std::cout << ",\n";
  }
  std::cout << "];\n";
  std::cout << "export const binary = [\n";
  for (const std::uint8_t opcode :
       {0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x1b, 0x0f, 0x10, 0x11, 0x12,
        0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x28, 0x27})
    Bytecode({opcode, 0, 0});
  std::cout << "];\nexport const unary = [\n";
  Bytecode({0x1f, 0});
  Bytecode({0x20, 0});
  Bytecode({0x23});
  Bytecode({0x24});
  Bytecode({0x1c, 0});
  Bytecode({0x1d, 0});
  Bytecode({0xfc, 8, 0});
  Bytecode({0xfc, 5, 0});
  std::cout << "];\nexport const special = [\n";
  for (const std::uint8_t opcode : {0, 1, 2, 3, 0x6a, 0x6b}) Bytecode({opcode});
  std::cout << "];\nexport const numbers = [\n";
  const auto number = [](std::uint64_t bits) {
    constexpr std::uint64_t kExponent = UINT64_C(0x7ff0000000000000);
    constexpr std::uint64_t kFraction = UINT64_C(0x000fffffffffffff);
    if ((bits & kExponent) == kExponent && (bits & kFraction) != 0) return;
    std::cout << "[0x" << std::hex << bits << "n," << std::dec;
    Emit(ScalarValue{Float64Bits{bits}});
    std::cout << "],\n";
  };
  for (const auto bits :
       {UINT64_C(0), UINT64_C(1), UINT64_C(0x8000000000000000),
        UINT64_C(0x7fefffffffffffff), UINT64_C(0x7ff0000000000000),
        UINT64_C(0xfff0000000000000), UINT64_C(0x0010000000000000)})
    number(bits);
  std::mt19937_64 random(0x51a);
  constexpr std::size_t kRandomNumbers = 4096;
  for (std::size_t i = 0; i < kRandomNumbers; ++i) number(random());
  std::cout << "];\nexport const negativeBase = ";
  Emit(BinaryExpression{BinaryOperator::kExponent, IntegerLiteral{-2},
                        IntegerLiteral{3}});
  std::cout << ";\n";
  return std::cout.good() ? EXIT_SUCCESS : EXIT_FAILURE;
}
