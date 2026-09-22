#include "abcd/analysis/global_access.h"

#include <cstdlib>
#include <vector>

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace
int main() {
  using namespace abcd;
  using namespace analysis;
  struct Case {
    std::uint8_t opcode;
    bool wide;
    GlobalOp operation;
  };
  const Case cases[]{{0x3f, false, GlobalOp::kReadName},
                     {0x8c, true, GlobalOp::kReadName},
                     {0x40, false, GlobalOp::kWriteName},
                     {0x8d, true, GlobalOp::kWriteName},
                     {0x41, true, GlobalOp::kReadVar},
                     {0x7f, true, GlobalOp::kWriteVar},
                     {0x47, true, GlobalOp::kInitConst},
                     {0x48, true, GlobalOp::kInitMutable}};
  for (const auto& item : cases) {
    std::vector<std::uint8_t> bytes{item.opcode, 0xff};
    if (item.wide) bytes.push_back(0xff);
    bytes.insert(bytes.end(), {0xff, 0xff});
    const auto instruction = std::get<abc::DecodedInstruction>(
        abc::DecodeInstruction(bytes, abc::ProfileId::kDynamic1206));
    const auto access = std::get<GlobalAccess>(LiftGlobalAccess(instruction));
    Check(access.operation == item.operation &&
          access.name.value == UINT16_MAX);
    Check(ReadsGlobal(access.operation) ==
          (item.operation == GlobalOp::kReadName ||
           item.operation == GlobalOp::kReadVar));
    auto truncated = instruction;
    truncated.operand_count = 0;
    Check(std::get<GlobalAccessError>(LiftGlobalAccess(truncated)) ==
          GlobalAccessError::kMalformedInstruction);
    for (std::size_t i = 0; i < instruction.operand_count; ++i) {
      auto invalid = instruction;
      invalid.operands[i].raw_bits = UINT64_MAX;
      Check(std::get<GlobalAccessError>(LiftGlobalAccess(invalid)) ==
            GlobalAccessError::kMalformedInstruction);
      invalid = instruction;
      invalid.operands[i].kind = abc::OperandKind::kRegister;
      Check(std::get<GlobalAccessError>(LiftGlobalAccess(invalid)) ==
            GlobalAccessError::kMalformedInstruction);
    }
  }
  abc::DecodedInstruction unrelated;
  unrelated.opcode = abc::Opcode::kLDGLOBAL;
  Check(std::get<GlobalAccessError>(LiftGlobalAccess(unrelated)) ==
        GlobalAccessError::kUnsupportedOpcode);
}
