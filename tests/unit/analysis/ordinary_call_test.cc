#include "abcd/analysis/ordinary_call.h"

#include <cstdlib>
#include <iostream>
#include <vector>

#include "abcd/abc/method_body.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
abcd::abc::MethodBody Body(std::span<const std::uint8_t> bytes) {
  abcd::abc::MethodBody body;
  for (std::size_t pc = 0; pc < bytes.size();) {
    const auto decoded =
        std::get<abcd::abc::DecodedInstruction>(abcd::abc::DecodeInstruction(
            bytes.subspan(pc), abcd::abc::ProfileId::kDynamic1206));
    body.instructions.push_back(
        {{static_cast<std::uint32_t>(pc)}, decoded, {}});
    pc += decoded.size;
  }
  return body;
}
}  // namespace

int main() {
  using namespace abcd;
  using namespace analysis;
  const std::vector<std::vector<std::uint8_t>> encodings{
      {0x29, 0xff},          {0x2a, 0xff, 1},    {0x2b, 0xff, 2, 1},
      {0x2c, 0xff, 1, 1, 3}, {0x73, 0xff, 3, 1}, {0x73, 0, 0, 255}};
  const std::array<std::string_view, 4> names{"callee", "a", "b", "c"};
  const std::array<std::vector<std::uint32_t>, 6> arguments{
      std::vector<std::uint32_t>{}, {1}, {2, 1}, {1, 1, 3}, {1, 2, 3}, {}};
  for (std::size_t i = 0; i < encodings.size(); ++i) {
    const auto decoded = Body(encodings[i]).instructions.front().decoded;
    const auto call =
        std::get<OrdinaryCall>(LiftOrdinaryCall(decoded, names.size()));
    Check(call.Count() == arguments[i].size());
    for (std::uint32_t j = 0; j < call.Count(); ++j)
      Check(call.At(j)->value == arguments[i][j]);
    Check(!call.At(call.Count()));
    auto malformed = decoded;
    malformed.operand_count = 0;
    Check(std::get<CallError>(LiftOrdinaryCall(malformed, names.size())) ==
          CallError::kMalformedInstruction);
    for (std::size_t operand = 0; operand < decoded.operand_count; ++operand) {
      malformed = decoded;
      malformed.operands[operand].raw_bits = 256;
      Check(std::get<CallError>(LiftOrdinaryCall(malformed, UINT64_MAX)) ==
            CallError::kMalformedInstruction);
      malformed = decoded;
      malformed.operands[operand].kind = abc::OperandKind::kString;
      Check(std::get<CallError>(LiftOrdinaryCall(malformed, names.size())) ==
            CallError::kMalformedInstruction);
    }
  }
  const auto last_register = Body(std::array<std::uint8_t, 3>{0x2a, 0, 255})
                                 .instructions.front()
                                 .decoded;
  Check(std::get<OrdinaryCall>(LiftOrdinaryCall(last_register, 256))
            .At(0)
            ->value == 255);
  Check(std::get<CallError>(LiftOrdinaryCall(last_register, 255)) ==
        CallError::kRegisterRange);
  const auto empty_range = Body(std::array<std::uint8_t, 4>{0x73, 0, 0, 255})
                               .instructions.front()
                               .decoded;
  Check(std::get<OrdinaryCall>(LiftOrdinaryCall(empty_range, 0)).Count() == 0);
  const auto crossing_range =
      Body(std::array<std::uint8_t, 4>{0x73, 0, 255, 255})
          .instructions.front()
          .decoded;
  Check(std::get<OrdinaryCall>(LiftOrdinaryCall(crossing_range, 510))
            .At(254)
            ->value == 509);
  Check(std::get<CallError>(LiftOrdinaryCall(crossing_range, 509)) ==
        CallError::kRegisterRange);
  for (const std::uint8_t count : {200, 201, 255}) {
    const std::vector<std::uint8_t> code{0x60, 0, 0x73, 0, count, 1, 0x64};
    const auto body = Body(code);
    const auto& decoded = body.instructions[1].decoded;
    const auto call =
        std::get<OrdinaryCall>(LiftOrdinaryCall(decoded, count + 1));
    Check(call.Count() == count && call.At(count - 1)->value == count);
    Check(std::get<CallError>(LiftOrdinaryCall(decoded, count)) ==
          CallError::kRegisterRange);
  }
  for (const std::uint16_t count : {200, 201, 256, 300, 400, 800, 65535}) {
    abc::DecodedInstruction decoded{};
    decoded.opcode = abc::Opcode::kWIDE_CALLRANGE_PREF_IMM16_V8;
    decoded.operand_count = 2;
    decoded.operands[0].kind = abc::OperandKind::kImmediate;
    decoded.operands[0].raw_bits = count;
    decoded.operands[1].kind = abc::OperandKind::kRegister;
    decoded.operands[1].raw_bits = 7;
    const auto call = std::get<OrdinaryCall>(LiftOrdinaryCall(decoded, count + 7));
    Check(call.Count() == count && call.At(count - 1)->value == count + 6U);
    Check(call.range_semantics == RangeCallSemantics::kIsaLayout);
    Check(std::get<CallError>(LiftOrdinaryCall(decoded, count + 6)) == CallError::kRegisterRange);
    auto bad = decoded;
    bad.operands[0].raw_bits = UINT16_MAX + 1U;
    Check(std::get<CallError>(LiftOrdinaryCall(bad, UINT64_MAX)) == CallError::kMalformedInstruction);
    bad = decoded; bad.operands[1].raw_bits = 256;
    Check(std::get<CallError>(LiftOrdinaryCall(bad, UINT64_MAX)) == CallError::kMalformedInstruction);
  }
  {
    const auto decoded = Body(std::array<std::uint8_t, 3>{0x2d, 0, 0}).instructions.front().decoded;
    Check(std::get<CallError>(LiftOrdinaryCall(decoded, 4)) ==
          CallError::kUnsupportedOpcode);
  }
  {
    // DEPRECATED_CALLARG0 v3: register callee, no arguments.
    const auto decoded = Body(std::array<std::uint8_t, 3>{0xfc, 11, 3}).instructions.front().decoded;
    const auto call = std::get<OrdinaryCall>(LiftOrdinaryCall(decoded, 4));
    Check(call.Count() == 0 && std::get<abc::RegisterId>(call.callee).value == 3);
    Check(std::get<CallError>(LiftOrdinaryCall(decoded, 3)) == CallError::kRegisterRange);
  }
  {
    // DEPRECATED_CALLRANGE 2, v5: callee v5, arguments v6 and v7.
    abc::DecodedInstruction decoded{};
    decoded.opcode = abc::Opcode::kDEPRECATED_CALLRANGE_PREF_IMM16_V8;
    decoded.operand_count = 2;
    decoded.operands[0].kind = abc::OperandKind::kImmediate;
    decoded.operands[0].raw_bits = 2;
    decoded.operands[1].kind = abc::OperandKind::kRegister;
    decoded.operands[1].raw_bits = 5;
    const auto call = std::get<OrdinaryCall>(LiftOrdinaryCall(decoded, 8));
    Check(call.Count() == 2 && call.At(0)->value == 6 && call.At(1)->value == 7 &&
          std::get<abc::RegisterId>(call.callee).value == 5);
    Check(std::get<CallError>(LiftOrdinaryCall(decoded, 7)) == CallError::kRegisterRange);
  }
  {
    const auto decoded = Body(std::array<std::uint8_t, 5>{0x60, 0, 0x29, 0, 0x64}).instructions[1].decoded;
    const auto call = LiftOrdinaryCall(decoded, 1);
    if (const auto* value = std::get_if<OrdinaryCall>(&call))
      Check(std::holds_alternative<Accumulator>(value->callee));
  }
}
