#ifndef ABCD_SOURCE_SENDABLE_CLASS_H_
#define ABCD_SOURCE_SENDABLE_CLASS_H_
#include "class_tree.h"
namespace abcd::source {
// Primitive masks are an exact frontend encoding. Nominal/GENERIC/NONE need
// their own binding-backed spelling proof; never substitute any or erase them.
[[nodiscard]] bool PrimitiveSendableType(abc::SendableTypeMask);
[[nodiscard]] std::optional<SendableFailure> VerifySendableClass(
    const ClassTree&, bool finished, analysis::SemanticBudget = {}, std::uint64_t* work = nullptr);
}
#endif
