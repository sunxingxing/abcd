#include "abcd/analysis/function_creation.h"

#include <array>
#include <cstdlib>

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
}  // namespace
int main() {
  using namespace abcd;
  using analysis::CreationKind;
  using analysis::HomeObjectInput;
  struct Case {
    std::uint8_t opcode;
    bool wide_cache;
    CreationKind kind;
    HomeObjectInput home;
  };
  const Case cases[]{
      {0x33, false, CreationKind::kFunction, HomeObjectInput::kCurrentFunction},
      {0x74, true, CreationKind::kFunction, HomeObjectInput::kCurrentFunction},
      {0x34, false, CreationKind::kMethod, HomeObjectInput::kAccumulator},
      {0xbe, true, CreationKind::kMethod, HomeObjectInput::kAccumulator}};
  for (const auto& item : cases) {
    for (const std::uint16_t slot : {0, 200, 201, 65535}) {
      for (const std::uint8_t length : {0, 1, 200, 201, 255}) {
        std::array<std::uint8_t, 6> bytes{item.opcode, 0xff, 0xff, 0, 0, 0};
        const auto method_at = item.wide_cache ? 3 : 2;
        bytes[method_at] = slot & 0xff;
        bytes[method_at + 1] = slot >> 8;
        bytes[method_at + 2] = length;
        const auto decoded = std::get<abc::DecodedInstruction>(
            abc::DecodeInstruction(bytes, abc::ProfileId::kDynamic1206));
        const auto created = std::get<analysis::FunctionCreation>(
            analysis::LiftFunctionCreation(decoded));
        Check(created.kind == item.kind && created.home_object == item.home);
        Check(created.method.value == slot && created.length.value == length);
        for (std::size_t operand = 0; operand < decoded.operand_count;
             ++operand) {
          auto malformed = decoded;
          malformed.operands[operand].raw_bits = UINT64_MAX;
          Check(std::get<analysis::CreationError>(
                    analysis::LiftFunctionCreation(malformed)) ==
                analysis::CreationError::kMalformedInstruction);
        }
        auto malformed = decoded;
        malformed.operand_count = 0;
        Check(std::get<analysis::CreationError>(
                  analysis::LiftFunctionCreation(malformed)) ==
              analysis::CreationError::kMalformedInstruction);
        malformed = decoded;
        malformed.operands[1].kind = abc::OperandKind::kString;
        Check(std::get<analysis::CreationError>(
                  analysis::LiftFunctionCreation(malformed)) ==
              analysis::CreationError::kMalformedInstruction);
      }
    }
  }
  Check(std::get<analysis::CreationError>(analysis::LiftFunctionCreation({})) ==
        analysis::CreationError::kUnsupportedOpcode);
}
