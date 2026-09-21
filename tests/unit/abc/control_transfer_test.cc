#include "abcd/abc/control_transfer.h"

#include <array>
#include <cstdint>
#include <cstdlib>

namespace {

struct EncodingCase {
  std::uint16_t opcode;
  std::uint8_t size;
};

#include "abcd/abc/generated_isa_cases.inc"

void Check(bool condition) {
  if (!condition) std::abort();
}

}  // namespace

int main() {
  using abcd::abc::ControlTransfer;
  using abcd::abc::GetControlTransfer;
  using abcd::abc::Opcode;

  Check(GetControlTransfer(Opcode::kLDUNDEFINED) ==
        ControlTransfer::kFallthrough);
  Check(GetControlTransfer(Opcode::kJMP_IMM8) == ControlTransfer::kJump);
  Check(GetControlTransfer(Opcode::kJEQZ_IMM8) == ControlTransfer::kBranch);
  Check(GetControlTransfer(Opcode::kRETURN) == ControlTransfer::kReturn);
  Check(GetControlTransfer(Opcode::kRETURNUNDEFINED) ==
        ControlTransfer::kReturn);
  Check(GetControlTransfer(Opcode::kTHROW_PREF_NONE) ==
        ControlTransfer::kThrow);
  Check(GetControlTransfer(Opcode::kTHROW_IFNOTOBJECT_PREF_V8) ==
        ControlTransfer::kFallthrough);
  Check(GetControlTransfer(Opcode::kSUSPENDGENERATOR_V8) ==
        ControlTransfer::kSuspend);
  Check(GetControlTransfer(Opcode::kDEPRECATED_SUSPENDGENERATOR_PREF_V8_V8) ==
        ControlTransfer::kSuspend);
  Check(GetControlTransfer(Opcode::kASYNCGENERATORRESOLVE_V8_V8_V8) ==
        ControlTransfer::kSuspend);
  Check(GetControlTransfer(Opcode::kASYNCFUNCTIONAWAITUNCAUGHT_V8) ==
        ControlTransfer::kFallthrough);

  std::array<bool, UINT16_MAX + 1> known{};
  for (const EncodingCase encoding : kEncodingCases) {
    known[encoding.opcode] = true;
    Check(GetControlTransfer(static_cast<Opcode>(encoding.opcode)).has_value());
  }
  for (std::uint32_t value = 0; value <= UINT16_MAX; ++value) {
    if (!known[value]) {
      Check(!GetControlTransfer(static_cast<Opcode>(value)).has_value());
    }
  }
}
