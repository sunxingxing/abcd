#ifndef ABCD_ANALYSIS_LEXICAL_H_
#define ABCD_ANALYSIS_LEXICAL_H_

#include "abcd/abc/pool_reference.h"
#include "abcd/analysis/scalar.h"

namespace abcd::analysis {
struct LexicalDepth {
  std::uint16_t value = 0;
};
struct LexicalSlot {
  std::uint16_t value = 0;
};
struct LexicalAddress {
  LexicalDepth depth;
  LexicalSlot slot;
};
struct LexicalLoad {
  LexicalAddress address;
};
struct LexicalStore {
  LexicalAddress address;
  ScalarSlot value;
};
struct PushEnvironment {
  std::uint16_t slots = 0;
  std::optional<abc::PoolIndex> scope;
};
struct PopEnvironment {};
struct CurrentEnvironment {};
enum class LexicalError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};
using LexicalResult =
    std::variant<LexicalLoad, LexicalStore, PushEnvironment, PopEnvironment,
                 CurrentEnvironment, LexicalError>;

// Requires DecodeInstruction output. Load writes acc; store preserves acc;
// push installs a new child env AND writes its internal identity to acc; pop
// installs the parent without changing acc; CurrentEnvironment writes env to
// acc. New slots contain Hole, not undefined. Loading a slot does not itself
// imply a JS TDZ check. Depth zero selects current env, not its parent.
// No allocation/traversal. Scope is a regional literal-array slot. Environment
// bounds, captures, sentinel flow and source variable binding remain separate.
[[nodiscard]] LexicalResult LiftLexicalInstruction(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_LEXICAL_H_
