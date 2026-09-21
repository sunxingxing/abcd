#include "abcd/abc/control_transfer.h"

namespace abcd::abc {

std::optional<ControlTransfer> GetControlTransfer(Opcode opcode) noexcept {
  // Pinned ets_runtime interpreter-inl.cpp:3295,5361,5395 returns to caller;
  // runtime_stubs-inl.h:425,1362 saves a continuation. Other await operations
  // retain ordinary normal fallthrough.
  switch (opcode) {
    case Opcode::kSUSPENDGENERATOR_V8:
    case Opcode::kDEPRECATED_SUSPENDGENERATOR_PREF_V8_V8:
    case Opcode::kASYNCGENERATORRESOLVE_V8_V8_V8:
      return ControlTransfer::kSuspend;
    default:
      break;
  }
  switch (opcode) {
#include "abcd/abc/generated_isa_flow.inc"
    default:
      return std::nullopt;
  }
}

}  // namespace abcd::abc
