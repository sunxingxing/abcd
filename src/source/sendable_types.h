#ifndef ABCD_SOURCE_SENDABLE_TYPES_H_
#define ABCD_SOURCE_SENDABLE_TYPES_H_
#include "class_tree.h"
namespace abcd::source {
struct SendableTypeCost { std::size_t allocated_bytes=0; std::uint64_t work=0; };
// GENERIC loses the nominal source type. Choose only a real imported constructor
// path already used by the corresponding instance assignment. This is a verified
// recompilation spelling, not recovery of the unique original annotation.
[[nodiscard]] std::variant<SendableTypeCost,SendableFailure> PrepareSendableTypes(ClassTree&,analysis::SemanticBudget={});
[[nodiscard]] std::variant<SendableTypeCost,SendableFailure> VerifySendableTypes(const ClassTree&,analysis::SemanticBudget={});
}
#endif
