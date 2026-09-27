#ifndef ABCD_ANALYSIS_EXCEPTION_INPUTS_H_
#define ABCD_ANALYSIS_EXCEPTION_INPUTS_H_

#include "abcd/analysis/exception_environment.h"
#include "abcd/analysis/exception_value_flow.h"

namespace abcd::analysis {
enum class ExceptionInputError : std::uint8_t {
  kMismatchedAnalysis, kInternalValue, kResourceLimit, kArgumentOwner
};
struct ExceptionInputFailure {
  ExceptionInputError kind;
  abc::CodeOffset pc;
  std::optional<ExceptionNodeId> node;
  std::optional<SsaValueId> value;
};
struct ExceptionInputProof {
  std::uint64_t reads = 0;
  std::uint64_t work_units = 0;
  std::optional<ArgumentFrameFact> arguments;
};

// Requires analyses of the same unchanged phased flow. Checks every reachable
// Before read against the converged value categories; internal VM values may
// not enter source operations (including stores, returns and explicit throws).
// Environment presence establishes reachability, not cell initialization.
// This is conditional on the value solver's owner premises. It does not
// discharge capture/module bindings, prove host semantics or permit code motion.
// Source publication still needs typed regions, AST validation and owner proof.
// Snapshot inputs additionally need an owner-certified invocation fact matching
// the retained layout, including zero declared slots. The fact is a premise,
// not inferred from that layout; callers must retain their callable identity.
// O(N+U+S log V) for nodes, reads, snapshots and SSA values; O(1) storage.
[[nodiscard]] std::variant<ExceptionInputProof, ExceptionInputFailure>
VerifyExceptionInputs(const ExceptionFlow& flow, const ExceptionSsa& ssa,
                      const EnvironmentFlow& environments,
                      const ExceptionValueFlow& values,
                      std::uint64_t work = kDefaultSemanticWork,
                      const ArgumentFrameFact* arguments = nullptr);
}  // namespace abcd::analysis
#endif
