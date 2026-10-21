#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/value_write.h"
#include "analysis/ssa_fixture.h"
#include <bit>

int main() {
  using namespace abcd;
  using namespace analysis;
  for (const auto bytes : {std::vector<std::uint8_t>{0x3b, 0, 0, 0xff, 0xff},
       std::vector<std::uint8_t>{0x89, 0, 0, 0, 0xff, 0xff},
       std::vector<std::uint8_t>{0xfd, 9, 0, 0xff, 0xff, 0xff, 0xff}}) {
    const auto body = test::Body(bytes);
    const auto& site = body.instructions.front();
    const auto write = std::get<ValueWrite>(LiftValueWrite(site.decoded, 1));
    test::Check(write.object.value == 0);
    const auto index = std::bit_cast<double>(std::get<Float64Bits>(write.key).value);
    test::Check(index == (bytes.front() == 0xfd ? double(UINT32_MAX) : double(UINT16_MAX)));
    const auto lifted = std::get<SemanticInstruction>(LiftSemanticInstruction(site, abc::ProfileId::kDynamic1206, 1));
    test::Check(std::holds_alternative<ValueWrite>(lifted.operation));
    test::Check(lifted.effects.exceptional == ExceptionalCompletion::kMayThrow);
    test::Check(std::get<ValueWriteError>(LiftValueWrite(site.decoded, 0)) == ValueWriteError::kRegisterRange);
    for (std::size_t i = 0; i < site.decoded.operand_count; ++i) {
      auto bad = site.decoded; bad.operands[i].raw_bits = UINT64_MAX;
      test::Check(std::get<ValueWriteError>(LiftValueWrite(bad, 1)) == ValueWriteError::kMalformedInstruction);
      bad = site.decoded; bad.operands[i].kind = abc::OperandKind::kString;
      test::Check(std::get<ValueWriteError>(LiftValueWrite(bad, 1)) == ValueWriteError::kMalformedInstruction);
    }
  }
  for (const std::size_t n : {200, 201, 400, 800}) {
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0; i < n; ++i) {
      // Repeated independently observable Get/Set, not an allocation template.
      bytes.insert(bytes.end(), {0x60, 0, 0xfd, 8, 0xff, 0xff, 0xff, 0xff,
                                 0xfd, 9, 0, 0xff, 0xff, 0xff, 0xff});
    }
    bytes.push_back(0x65);  // returnundefined; generated opcode checked below.
    test::Check(abc::Opcode::kLDA_V8 == static_cast<abc::Opcode>(0x60));
    test::Check(abc::Opcode::kRETURNUNDEFINED == static_cast<abc::Opcode>(0x65));
    const auto body = test::Body(bytes);
    std::size_t lo = 0, hi = 64 * 1024 * 1024;
    while (lo < hi) {
      const auto mid = lo + (hi - lo) / 2;
      const auto result = BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, 1, {mid});
      if (std::holds_alternative<SemanticMethod>(result)) hi = mid;
      else {
        test::Check(std::get<SemanticFailure>(result).kind == SemanticError::kResourceLimit);
        lo = mid + 1;
      }
    }
    test::Check(std::holds_alternative<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, 1, {lo})));
    test::Check(std::holds_alternative<SemanticFailure>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, 1, {lo - 1})));
  }
}
