#include "abcd/analysis/named_write.h"

#include <cstdlib>
#include <vector>

#include "abcd/analysis/accumulator_flow.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
abcd::abc::DecodedInstruction Decode(
    std::initializer_list<std::uint8_t> bytes) {
  const std::vector<std::uint8_t> encoded(bytes);
  return std::get<abcd::abc::DecodedInstruction>(abcd::abc::DecodeInstruction(
      encoded, abcd::abc::ProfileId::kDynamic1206));
}
}  // namespace

int main() {
  using namespace abcd;
  using namespace analysis;
  for (const auto& decoded : {Decode({0x43, 0xff, 0xff, 0xff, 201}),
                              Decode({0x91, 0xff, 0xff, 0xff, 0xff, 201})}) {
    for (const auto slot : {0U, 200U, 201U, 65535U}) {
      auto instruction = decoded;
      instruction.operands[1].raw_bits = slot;
      const auto write = std::get<NamedWrite>(LiftNamedWrite(instruction, 202));
      Check(write.name.value == slot && write.object.value == 201);
    }
    Check(std::get<NamedWriteError>(LiftNamedWrite(decoded, 201)) ==
          NamedWriteError::kRegisterRange);
    for (const auto operand : {0U, 1U, 2U}) {
      auto malformed = decoded;
      malformed.operands[operand].raw_bits = UINT64_MAX;
      Check(std::get<NamedWriteError>(LiftNamedWrite(malformed, UINT64_MAX)) ==
            NamedWriteError::kMalformedInstruction);
      malformed = decoded;
      malformed.operands[operand].kind = abc::OperandKind::kLiteralArray;
      Check(std::get<NamedWriteError>(LiftNamedWrite(malformed, 202)) ==
            NamedWriteError::kMalformedInstruction);
    }
    auto malformed = decoded;
    malformed.operand_count = 2;
    Check(std::get<NamedWriteError>(LiftNamedWrite(malformed, 202)) ==
          NamedWriteError::kMalformedInstruction);
  }
  auto narrow = Decode({0x43, 0, 0, 0, 0});
  narrow.operands[0].raw_bits = 256;
  Check(std::get<NamedWriteError>(LiftNamedWrite(narrow, 1)) ==
        NamedWriteError::kMalformedInstruction);
  Check(std::get<NamedWriteError>(
            LiftNamedWrite(Decode({0x7a, 0, 0, 0, 0}), 1)) ==
        NamedWriteError::kUnsupportedOpcode);
  abc::MethodBody body;
  body.instructions.push_back({{0}, Decode({0x43, 0, 0, 0, 0}), {}});
  body.instructions.push_back({{5}, Decode({0x64}), {}});
  const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
  const auto rejected =
      std::get<AccumulatorFailure>(AnalyzeAcyclicAccumulator(body, graph, 1));
  Check(rejected.kind == AccumulatorError::kReadBeforeValue &&
        rejected.offset.value == 0);
}
