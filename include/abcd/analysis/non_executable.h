#ifndef ABCD_ANALYSIS_NON_EXECUTABLE_H_
#define ABCD_ANALYSIS_NON_EXECUTABLE_H_
#include "abcd/abc/instruction.h"
#include <array>
#include <optional>
namespace abcd::analysis {
// Pinned ets_runtime f04900cf: interpreter-inl.cpp:6935-7046 is FATAL;
// interpreter_stub.cpp dispatches NOP; slowpath_lowering.cpp:783-811
// supplies no lowering. This is not an equality predicate or a JS effect.
// Evidence: docs/design/source-recovery/isa-branches-runtime-evidence.md.
enum class RuntimeEvidence : std::uint8_t { kPinnedExtendedBranches = 0 };
struct NonExecutableEvidence {
  abc::Opcode opcode;
  RuntimeEvidence evidence;
  friend bool operator==(const NonExecutableEvidence&, const NonExecutableEvidence&) = default;
};
inline constexpr std::array<abc::Opcode, 28> kNonExecutableBranches{{
    abc::Opcode::kJSTRICTEQZ_IMM8,
    abc::Opcode::kJSTRICTEQZ_IMM16,
    abc::Opcode::kJNSTRICTEQZ_IMM8,
    abc::Opcode::kJNSTRICTEQZ_IMM16,
    abc::Opcode::kJEQNULL_IMM8,
    abc::Opcode::kJEQNULL_IMM16,
    abc::Opcode::kJNENULL_IMM8,
    abc::Opcode::kJNENULL_IMM16,
    abc::Opcode::kJSTRICTEQNULL_IMM8,
    abc::Opcode::kJSTRICTEQNULL_IMM16,
    abc::Opcode::kJNSTRICTEQNULL_IMM8,
    abc::Opcode::kJNSTRICTEQNULL_IMM16,
    abc::Opcode::kJEQUNDEFINED_IMM8,
    abc::Opcode::kJEQUNDEFINED_IMM16,
    abc::Opcode::kJNEUNDEFINED_IMM8,
    abc::Opcode::kJNEUNDEFINED_IMM16,
    abc::Opcode::kJSTRICTEQUNDEFINED_IMM8,
    abc::Opcode::kJSTRICTEQUNDEFINED_IMM16,
    abc::Opcode::kJNSTRICTEQUNDEFINED_IMM8,
    abc::Opcode::kJNSTRICTEQUNDEFINED_IMM16,
    abc::Opcode::kJEQ_V8_IMM8,
    abc::Opcode::kJEQ_V8_IMM16,
    abc::Opcode::kJNE_V8_IMM8,
    abc::Opcode::kJNE_V8_IMM16,
    abc::Opcode::kJSTRICTEQ_V8_IMM8,
    abc::Opcode::kJSTRICTEQ_V8_IMM16,
    abc::Opcode::kJNSTRICTEQ_V8_IMM8,
    abc::Opcode::kJNSTRICTEQ_V8_IMM16
}};
inline std::optional<NonExecutableEvidence> NonExecutable(abc::Opcode opcode) {
  for (auto item : kNonExecutableBranches)
    if (item == opcode) return NonExecutableEvidence{opcode, RuntimeEvidence::kPinnedExtendedBranches};
  return std::nullopt;
}
}  // namespace abcd::analysis
#endif
