#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/isa_catalog.h"
#include <array>
#include <cstdlib>
#include <variant>
namespace { void Check(bool ok) { if (!ok) std::abort(); } }
int main() {
  using namespace abcd;
  static_assert(static_cast<unsigned>(analysis::SemanticError::kResourceLimit) == 6);
  static_assert(static_cast<unsigned>(analysis::SemanticError::kNonExecutableInstruction) == 7);
  for (const auto n : {200,201,400,800}) {
    for (int i=0;i<n;++i) for (const auto opcode : analysis::kNonExecutableBranches) {
      const auto* spec = analysis::FindPinnedInstruction(opcode);
      Check(spec != nullptr);
      std::array<std::uint8_t, 8> bytes{};
      bytes[0] = static_cast<std::uint8_t>(opcode);
      const auto decoded = abc::DecodeInstruction(std::span(bytes).first(spec->size), abc::ProfileId::kDynamic1206);
      const auto* instruction = std::get_if<abc::DecodedInstruction>(&decoded);
      Check(instruction != nullptr);
      abc::BodyInstruction site{{0}, *instruction, abc::CodeOffset{0}};
      const auto lifted = analysis::LiftSemanticInstruction(site, abc::ProfileId::kDynamic1206, 4);
      const auto* failure = std::get_if<analysis::SemanticFailure>(&lifted);
      Check(failure && failure->kind == analysis::SemanticError::kNonExecutableInstruction);
      Check(failure->pc.value == 0 && failure->non_executable == analysis::NonExecutable(opcode));
      // A missing resolved target is malformed input, not runtime evidence.
      site.branch_target.reset();
      Check(std::get<analysis::SemanticFailure>(analysis::LiftSemanticInstruction(site,
          abc::ProfileId::kDynamic1206,4)).kind == analysis::SemanticError::kMalformedInstruction);
    }
  }
  Check(!analysis::NonExecutable(abc::Opcode::kJEQZ_IMM8));
  Check(!analysis::NonExecutable(abc::Opcode::kJNEZ_IMM16));
  Check(!analysis::NonExecutable(abc::Opcode::kDYNAMICIMPORT));
}
