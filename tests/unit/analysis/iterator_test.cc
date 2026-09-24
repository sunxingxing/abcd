#include "abcd/analysis/iterator.h"

#include <array>
#include <cstdlib>
#include <span>

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
template <std::size_t N>
abcd::abc::DecodedInstruction Decode(const std::array<std::uint8_t, N>& bytes) {
  return std::get<abcd::abc::DecodedInstruction>(
      abcd::abc::DecodeInstruction(bytes, abcd::abc::ProfileId::kDynamic1301));
}
}  // namespace

int main() {
  using namespace abcd;
  using namespace analysis;
  const auto property = Decode(std::array<std::uint8_t, 1>{0x66});
  Check(std::holds_alternative<PropertyIterator>(
      LiftIteratorInstruction(property, 2)));
  for (const auto& decoded : {
           Decode(std::array<std::uint8_t, 2>{0x67, 0x7f}),
           Decode(std::array<std::uint8_t, 3>{0xab, 0x7f, 0x00})}) {
    const auto operation = LiftIteratorInstruction(decoded, 2);
    const auto& acquire = std::get<AcquireIterator>(operation);
    Check(acquire.mode == IteratorMode::kSync);
    Check(acquire.inline_cache == 0x7f);
  }
  const auto wide = Decode(std::array<std::uint8_t, 3>{0xab, 0x34, 0x12});
  Check(std::get<AcquireIterator>(LiftIteratorInstruction(wide, 2))
            .inline_cache == 0x1234);
  auto bad_cache = wide;
  bad_cache.operands[0].raw_bits = 0x10000;
  Check(std::get<IteratorError>(LiftIteratorInstruction(bad_cache, 2)) ==
        IteratorError::kMalformedInstruction);
  auto bad_property = property;
  bad_property.operand_count = 1;
  Check(std::get<IteratorError>(LiftIteratorInstruction(bad_property, 2)) ==
        IteratorError::kMalformedInstruction);
  const auto async = Decode(std::array<std::uint8_t, 2>{0xd7, 0x42});
  const auto acquire =
      std::get<AcquireIterator>(LiftIteratorInstruction(async, 2));
  Check(acquire.mode == IteratorMode::kAsync);
  Check(acquire.inline_cache == 0x42);
  for (const auto& decoded : {
           Decode(std::array<std::uint8_t, 3>{0x68, 0x7f, 0x01}),
           Decode(std::array<std::uint8_t, 4>{0xac, 0x7f, 0x00, 0x01})}) {
    const auto operation = LiftIteratorInstruction(decoded, 2);
    const auto& close = std::get<CloseIterator>(operation);
    Check(close.inline_cache == 0x7f && close.iterator.value == 1);
    Check(std::get<IteratorError>(LiftIteratorInstruction(decoded, 1)) ==
          IteratorError::kRegisterRange);
    auto malformed = decoded;
    malformed.operands[1].kind = abc::OperandKind::kImmediate;
    Check(std::get<IteratorError>(LiftIteratorInstruction(malformed, 2)) ==
          IteratorError::kMalformedInstruction);
    malformed = decoded;
    malformed.operands[1].raw_bits = 256;
    Check(std::get<IteratorError>(LiftIteratorInstruction(malformed, 300)) ==
          IteratorError::kMalformedInstruction);
  }
  const auto next =
      Decode(std::array<std::uint8_t, 4>{0xfc, 0x02, 0x00, 0x01});
  const auto advance =
      std::get<IteratorNext>(LiftIteratorInstruction(next, 2));
  Check(advance.iterator.value == 0 && advance.method.value == 1);
  Check(std::get<IteratorError>(LiftIteratorInstruction(next, 1)) ==
        IteratorError::kRegisterRange);
  const auto property_name = Decode(std::array<std::uint8_t, 2>{0x36, 0x01});
  Check(std::get<NextPropertyName>(LiftIteratorInstruction(property_name, 2))
            .iterator.value == 1);
  Check(std::get<IteratorError>(LiftIteratorInstruction(property_name, 1)) ==
        IteratorError::kRegisterRange);
  auto malformed_name = property_name;
  malformed_name.operands[0].kind = abc::OperandKind::kImmediate;
  Check(std::get<IteratorError>(LiftIteratorInstruction(malformed_name, 2)) ==
        IteratorError::kMalformedInstruction);
  auto malformed = next;
  malformed.operand_count = 1;
  Check(std::get<IteratorError>(LiftIteratorInstruction(malformed, 2)) ==
        IteratorError::kMalformedInstruction);
  malformed = async;
  malformed.operands[0].raw_bits = 256;
  Check(std::get<IteratorError>(LiftIteratorInstruction(malformed, 2)) ==
        IteratorError::kMalformedInstruction);
  malformed = next;
  malformed.operands[0].kind = abc::OperandKind::kImmediate;
  Check(std::get<IteratorError>(LiftIteratorInstruction(malformed, 2)) ==
        IteratorError::kMalformedInstruction);
  const auto unrelated = Decode(std::array<std::uint8_t, 1>{0x64});
  Check(std::get<IteratorError>(LiftIteratorInstruction(unrelated, 2)) ==
        IteratorError::kUnsupportedOpcode);
}
