#ifndef ABCD_ANALYSIS_OBJECT_CREATION_H_
#define ABCD_ANALYSIS_OBJECT_CREATION_H_

#include "abcd/abc/pool_reference.h"
#include "abcd/abc/instruction.h"

namespace abcd::analysis {
struct EmptyObjectCreation {};
struct BufferedObjectCreation {
  abc::PoolIndex literal;
};
enum class ObjectCreationError { kUnsupportedOpcode, kMalformedInstruction };

// CREATEEMPTYOBJECT has no pool reference. Pinned modern buffered creation
// uses an IC operand followed by a regional literal-array slot; deprecated
// creation has a different layout and remains rejected here.
[[nodiscard]] std::variant<EmptyObjectCreation, BufferedObjectCreation,
                           ObjectCreationError>
LiftObjectCreation(const abc::DecodedInstruction& instruction);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_OBJECT_CREATION_H_
