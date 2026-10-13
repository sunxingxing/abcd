#include "abcd/analysis/module_load.h"

#include "abcd/analysis/semantic_ir.h"
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
    std::vector<std::uint8_t> bytes;
    ModuleSpace space;
    std::uint16_t slot;
  };
  const Case cases[]{
      {{0x7d, 201}, ModuleSpace::kLocal, 201},
      {{0x7e, 255}, ModuleSpace::kExternal, 255},
      {{0xfd, 0x10, 0xff, 0xff}, ModuleSpace::kLocal, UINT16_MAX},
      {{0xfd, 0x11, 0xff, 0xff}, ModuleSpace::kExternal, UINT16_MAX}};
  for (const auto& item : cases) {
    const auto decoded = std::get<abc::DecodedInstruction>(
        abc::DecodeInstruction(item.bytes, abc::ProfileId::kDynamic1206));
    const auto load = std::get<ModuleLoad>(LiftModuleLoad(decoded));
    Check(load.space == item.space && load.slot.value == item.slot);
    auto bad = decoded;
    bad.operands[0].raw_bits = UINT64_MAX;
    Check(std::get<ModuleLoadError>(LiftModuleLoad(bad)) ==
          ModuleLoadError::kMalformedInstruction);
    bad = decoded;
    bad.operands[0].kind = abc::OperandKind::kString;
    Check(std::get<ModuleLoadError>(LiftModuleLoad(bad)) ==
          ModuleLoadError::kMalformedInstruction);
    bad = decoded;
    bad.operand_count = 0;
    Check(std::get<ModuleLoadError>(LiftModuleLoad(bad)) ==
          ModuleLoadError::kMalformedInstruction);
  }
  abc::DecodedInstruction other;
  other.opcode = abc::Opcode::kLDGLOBAL;
  Check(std::get<ModuleLoadError>(LiftModuleLoad(other)) ==
        ModuleLoadError::kUnsupportedOpcode);
  for (const auto bytes : {std::vector<std::uint8_t>{0xfb, 0x15, 201}, {0xfb, 0x16, 201, 0}}) {
    auto decoded = std::get<abc::DecodedInstruction>(abc::DecodeInstruction(bytes, abc::ProfileId::kDynamic2400));
    const auto load = std::get<ModuleLoad>(LiftModuleLoad(decoded));
    Check(load.space == ModuleSpace::kExternal && load.slot.value == 201 && load.loading == ModuleLoading::kLazy);
    const auto lifted = LiftSemanticInstruction({{}, decoded}, abc::ProfileId::kDynamic2400, 3);
    const auto& semantic = std::get<SemanticInstruction>(lifted);
    Check(semantic.effects.normal == ModuleReadEffects(ModuleLoading::kLazy));
    Check(semantic.effects.before_throw == semantic.effects.normal);
    Check(semantic.effects.exceptional == ExceptionalCompletion::kMayThrow);
    Check(semantic.effects.normal_registers.write && std::holds_alternative<Accumulator>(*semantic.effects.normal_registers.write));
    decoded.operand_count = 0;
    Check(std::get<ModuleLoadError>(LiftModuleLoad(decoded)) == ModuleLoadError::kMalformedInstruction);
  }
  for (const auto opcode : {abc::Opcode::kCALLRUNTIME_LDLAZYSENDABLEMODULEVAR_PREF_IMM8,
                           abc::Opcode::kCALLRUNTIME_WIDELDLAZYSENDABLEMODULEVAR_PREF_IMM16}) {
    abc::DecodedInstruction decoded; decoded.opcode = opcode;
    Check(std::get<ModuleLoadError>(LiftModuleLoad(decoded)) == ModuleLoadError::kUnsupportedOpcode);
  }

}
