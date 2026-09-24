#ifndef ABCD_ANALYSIS_EXCEPTION_EDGE_H_
#define ABCD_ANALYSIS_EXCEPTION_EDGE_H_

#include "abcd/analysis/exception_dispatch.h"
#include "abcd/analysis/semantic_ir.h"

namespace abcd::analysis {
struct PendingException {};
struct ThrownValue {
  ScalarValue value;
};
using ExceptionValue = std::variant<PendingException, ThrownValue>;
struct ExceptionEdge {
  // Snapshot register inputs at this instruction, before its normal write.
  // Handler acc comes from exception, never from that normal destination.
  abc::CodeOffset before;
  std::optional<DispatchTarget> target;  // Absent means propagation to caller.
  ExceptionValue exception;
  ExceptionalCompletion completion;
};

// Inputs must describe the same decoded method/profile. This selects an edge,
// not handler-entry SSA or a lexical-environment restoration proof. Explicit
// thrown values still require Language validation before source admission.
// O(log R) lookup in the existing dispatch index, no allocation.
[[nodiscard]] std::optional<ExceptionEdge> ResolveExceptionEdge(
    const SemanticInstruction& instruction, const ExceptionDispatch& dispatch);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_EXCEPTION_EDGE_H_
