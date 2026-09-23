#ifndef ABCD_ANALYSIS_OBJECT_CREATION_H_
#define ABCD_ANALYSIS_OBJECT_CREATION_H_

#include "abcd/abc/pool_reference.h"
#include "abcd/abc/instruction.h"

namespace abcd::analysis {
struct ObjectCreation {
  abc::PoolIndex literal;
};
enum class ObjectCreationError { kUnsupportedOpcode, kMalformedInstruction };

// Pinned modern CREATEOBJECTWITHBUFFER uses an IC operand followed by a
// regional literal-array slot. Deprecated creation has a different layout.
[[nodiscard]] std::variant<ObjectCreation, ObjectCreationError>
LiftObjectCreation(const abc::DecodedInstruction& instruction);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_OBJECT_CREATION_H_
