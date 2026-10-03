#ifndef ABCD_SOURCE_IMPLICIT_CONSTRUCTOR_H_
#define ABCD_SOURCE_IMPLICIT_CONSTRUCTOR_H_
#include "derived_value_states.h"
namespace abcd::source {
struct ImplicitConstructorProof {
  analysis::OpId first_call;
  std::uint64_t work = 0;
  std::size_t allocated_bytes = 0;
};
// Pinned interpreter-inl.cpp HANDLE_CALLRUNTIME_SUPERCALLFORWARDALLARGS reads
// GetNewTarget/GetNumArgs; RuntimeSuperCallForwardAllArgs copies sp slots and
// dispatches IsConstructor/Construct. It does not invoke an array iterator.
// Native default derived constructors forward their raw invocation argument
// list without GetIterator. Prove equivalence over CFG/SSA, not a prologue.
// Multiple branch-local forwarding sites are allowed; every normal path must
// forward exactly once and terminate with that receiver. Other effects reject.
[[nodiscard]] std::variant<ImplicitConstructorProof, DerivedProjectionError>
ProveImplicitConstructor(const FileFunction&, analysis::SemanticBudget = {});
}
#endif
