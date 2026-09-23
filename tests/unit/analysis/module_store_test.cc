#include "abcd/analysis/module_store.h"

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
    std::uint16_t slot;
  };
  const Case cases[]{
      {{0x7c, 201}, 201},
      {{0xfd, 0x0f, 0xff, 0xff}, UINT16_MAX}};
  for (const auto& item : cases) {
    const auto decoded = std::get<abc::DecodedInstruction>(
        abc::DecodeInstruction(item.bytes, abc::ProfileId::kDynamic1206));
    const auto store = std::get<ModuleStore>(LiftModuleStore(decoded));
    Check(store.slot.value == item.slot);
    auto bad = decoded;
    bad.operands[0].raw_bits = UINT64_MAX;
    Check(std::get<ModuleStoreError>(LiftModuleStore(bad)) ==
          ModuleStoreError::kMalformedInstruction);
    bad = decoded;
    bad.operands[0].kind = abc::OperandKind::kString;
    Check(std::get<ModuleStoreError>(LiftModuleStore(bad)) ==
          ModuleStoreError::kMalformedInstruction);
    bad = decoded;
    bad.operand_count = 0;
    Check(std::get<ModuleStoreError>(LiftModuleStore(bad)) ==
          ModuleStoreError::kMalformedInstruction);
  }
  abc::DecodedInstruction other;
  other.opcode = abc::Opcode::kLDGLOBAL;
  Check(std::get<ModuleStoreError>(LiftModuleStore(other)) ==
        ModuleStoreError::kUnsupportedOpcode);
}
