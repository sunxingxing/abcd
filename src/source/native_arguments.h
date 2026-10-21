#ifndef ABCD_SOURCE_NATIVE_ARGUMENTS_H_
#define ABCD_SOURCE_NATIVE_ARGUMENTS_H_
#include "tree_checks.h"

namespace abcd::source {
enum class NativeArgumentsError : std::uint8_t {
  kOwner, kMultiplicity, kEntry, kParameter, kShadowed, kCorrespondence
};
struct NativeArgumentsProof {
  analysis::OpId snapshot;
  analysis::IrRevision revision;
  analysis::ArgumentFrameFact frame;
  bool operator==(const NativeArgumentsProof&) const = default;
};
// Entry means the unique snapshot is reached by the invocation's single normal
// prefix before user-observable events. No loop, handler or parameter rewrite
// may manufacture entry provenance. Scratch is prepaid and shared by all slots.
std::variant<NativeArgumentsProof,NativeArgumentsError> ProveNativeArguments(
    const analysis::ValueProgram&, CallableKind, TreeCheckBudget&);
bool VerifyNativeArguments(const analysis::ValueProgram&,CallableKind,
    const NativeArgumentsProof&,TreeCheckBudget&);
void VerifyNativeArgumentsNames(const FunctionTree&,TreeCheckBudget&);
}  // namespace abcd::source
#endif
