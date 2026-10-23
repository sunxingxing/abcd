#include "abcd/analysis/module_load.h"

namespace abcd::analysis {
std::variant<ModuleLoad, ModuleLoadError> LiftModuleLoad(
    const abc::DecodedInstruction& instruction) {
  ModuleSpace space;
  ModuleLoading loading = ModuleLoading::kOrdinary;
  ModuleDomain domain = ModuleDomain::kOrdinary;
  std::uint64_t limit = UINT8_MAX;
  bool request_namespace = false;
  switch (instruction.opcode) {
    // ModuleValueAccessor::GetModuleNamespaceInternal resolves
    // requestedModules[index] of the current module and returns its namespace.
    case abc::Opcode::kWIDE_GETMODULENAMESPACE_PREF_IMM16:
      limit = UINT16_MAX;
      [[fallthrough]];
    case abc::Opcode::kGETMODULENAMESPACE_IMM8:
      space = ModuleSpace::kExternal;
      request_namespace = true;
      break;
    case abc::Opcode::kCALLRUNTIME_WIDELDSENDABLEEXTERNALMODULEVAR_PREF_IMM16:
      limit = UINT16_MAX;
      [[fallthrough]];
    case abc::Opcode::kCALLRUNTIME_LDSENDABLEEXTERNALMODULEVAR_PREF_IMM8:
      space = ModuleSpace::kExternal;
      domain = ModuleDomain::kSendable;
      break;
    case abc::Opcode::kCALLRUNTIME_WIDELDLAZYMODULEVAR_PREF_IMM16:
      limit = UINT16_MAX;
      [[fallthrough]];
    case abc::Opcode::kCALLRUNTIME_LDLAZYMODULEVAR_PREF_IMM8:
      space = ModuleSpace::kExternal;
      loading = ModuleLoading::kLazy;
      break;
    case abc::Opcode::kWIDE_LDLOCALMODULEVAR_PREF_IMM16:
      limit = UINT16_MAX;
      [[fallthrough]];
    case abc::Opcode::kLDLOCALMODULEVAR_IMM8:
      space = ModuleSpace::kLocal;
      break;
    case abc::Opcode::kWIDE_LDEXTERNALMODULEVAR_PREF_IMM16:
      limit = UINT16_MAX;
      [[fallthrough]];
    case abc::Opcode::kLDEXTERNALMODULEVAR_IMM8:
      space = ModuleSpace::kExternal;
      break;
    default:
      return ModuleLoadError::kUnsupportedOpcode;
  }
  constexpr std::size_t kSlot = 0, kOperands = 1;
  if (instruction.operand_count != kOperands ||
      instruction.operands[kSlot].kind != abc::OperandKind::kImmediate ||
      instruction.operands[kSlot].raw_bits > limit)
    return ModuleLoadError::kMalformedInstruction;
  return ModuleLoad{
      space,
      {static_cast<std::uint16_t>(instruction.operands[kSlot].raw_bits)}, loading, domain,
      request_namespace};
}
}  // namespace abcd::analysis
