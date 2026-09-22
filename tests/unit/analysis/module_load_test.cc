#include "abcd/analysis/module_load.h"

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
}
