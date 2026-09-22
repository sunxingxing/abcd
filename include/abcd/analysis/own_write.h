#ifndef ABCD_ANALYSIS_OWN_WRITE_H_
#define ABCD_ANALYSIS_OWN_WRITE_H_
#include "abcd/abc/pool_reference.h"
#include "abcd/abc/register_access.h"
namespace abcd::analysis {
enum class OwnWriteKind { kStore, kDefineField };
struct OwnWrite {
  OwnWriteKind kind;
  abc::PoolIndex name;
  abc::RegisterId object;
};
enum class OwnWriteError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};
// STOWNBYNAME and DEFINEFIELDBYNAME are different runtime operations. Both
// read receiver and acc, preserve them on normal completion, and may throw.
// No proof of receiver=this, declaration placement or JS source equivalence.
[[nodiscard]] std::variant<OwnWrite, OwnWriteError> LiftOwnWrite(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::analysis
#endif
