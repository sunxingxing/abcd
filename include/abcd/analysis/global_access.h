#ifndef ABCD_ANALYSIS_GLOBAL_ACCESS_H_
#define ABCD_ANALYSIS_GLOBAL_ACCESS_H_

#include "abcd/abc/instruction.h"
#include "abcd/abc/pool_reference.h"

namespace abcd::analysis {
enum class GlobalOp {
  kReadName,
  kReadVar,
  kWriteName,
  kWriteVar,
  kInitConst,
  kInitMutable
};
struct GlobalAccess {
  GlobalOp operation;
  abc::PoolIndex name;
};
enum class GlobalAccessError { kUnsupportedOpcode, kMalformedInstruction };
// Pinned v7.0: name lookup checks global record before global object;
// Var opcodes retain their IC-dependent lookup domain: LoadMiss/StoreMiss can
// visit the record, unlike the interpreter's no-profile object fallback.
// All may throw. Reads replace acc, writes
// consume and preserve acc on normal completion; explicit frame slots survive.
// InitMutable is runtime record initialization, not proof of source `let`.
// No lexical/module binding identity, declaration placement or purity implied.
[[nodiscard]] std::variant<GlobalAccess, GlobalAccessError> LiftGlobalAccess(
    const abc::DecodedInstruction& instruction);
[[nodiscard]] bool ReadsGlobal(GlobalOp operation);
}  // namespace abcd::analysis
#endif
