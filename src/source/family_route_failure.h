#ifndef ABCD_SOURCE_FAMILY_ROUTE_FAILURE_H_
#define ABCD_SOURCE_FAMILY_ROUTE_FAILURE_H_

#include "family_emission.h"

#include <type_traits>
#include <variant>

namespace abcd::source {
// Records every typed enum on a failure path into the bounded chain, outermost
// first, through the typed envelopes (kind, cause/error/reason, variants and
// the optional nested errors). A nested RouteFailure contributes its chain.
// Compile-time typed traversal only; never parses messages or names.
template<class T> void CollectFailureCauses(const T& error, FailureCauseChain& out) {
  if constexpr (std::is_same_v<T, RouteFailure>) {
    out.Extend(error.causes);
  } else if constexpr (std::is_enum_v<T>) {
    AppendFailureCause(out, error);
  } else if constexpr (std::is_same_v<T, std::monostate>) {
  } else if constexpr (requires { std::variant_size<T>::value; }) {
    std::visit([&](const auto& nested) { CollectFailureCauses(nested, out); }, error);
  } else {
    if constexpr (requires { error.kind; }) CollectFailureCauses(error.kind, out);
    if constexpr (requires { error.cause; }) CollectFailureCauses(error.cause, out);
    else if constexpr (requires { error.error; }) CollectFailureCauses(error.error, out);
    else if constexpr (requires { error.reason; }) CollectFailureCauses(error.reason, out);
    if constexpr (std::is_same_v<T, FamilyFileFailure>) {
      if (error.family_creation) CollectFailureCauses(error.family_creation->reason, out);
      CollectFailureCauses(error.creation_cause, out);
    }
    if constexpr (std::is_same_v<T, analysis::SemanticProofFailure>)
      if (error.internal_value) CollectFailureCauses(error.internal_value->reason, out);
    if constexpr (std::is_same_v<T, EvaluationFailure>) {
      CollectFailureCauses(error.evaluation.obligation, out);
      if (error.evaluation.for_in) CollectFailureCauses(error.evaluation.for_in->kind, out);
    }
    if constexpr (requires { error.tree; })
      if (error.tree) {
        CollectFailureCauses(error.tree->obligation, out);
        if (error.tree->ownership) CollectFailureCauses(*error.tree->ownership, out);
      }
    if constexpr (requires { error.deferred; })
      if (error.deferred) CollectFailureCauses(error.deferred->reason, out);
    if constexpr (std::is_same_v<T, CallPlanFailure>) {
      if (error.reference) {
        if (error.reference->placement) CollectFailureCauses(*error.reference->placement, out);
        if (error.reference->temporary) CollectFailureCauses(*error.reference->temporary, out);
        if (error.reference->checked) CollectFailureCauses(error.reference->checked->kind, out);
      }
    }
    if constexpr (std::is_same_v<T, analysis::SemanticFailure>) CollectFailureCauses(error.ssa_cause, out);
    if constexpr (std::is_same_v<T, analysis::RegisterUseFailure>)
      if (error.instruction) CollectFailureCauses(*error.instruction, out);
    if constexpr (std::is_same_v<T, abc::BodyFailure>) {
      if (error.instruction_error) CollectFailureCauses(*error.instruction_error, out);
      if (error.file_error) CollectFailureCauses(*error.file_error, out);
    }
    if constexpr (std::is_same_v<T, BodyEmitFailure>) {
      if (error.scalar_error) CollectFailureCauses(*error.scalar_error, out);
      if (error.emit_error) CollectFailureCauses(*error.emit_error, out);
      if (error.metadata_error) CollectFailureCauses(*error.metadata_error, out);
      if (error.literal_error) CollectFailureCauses(*error.literal_error, out);
      if (error.object_error) CollectFailureCauses(*error.object_error, out);
    }
  }
}
// Preserve resource/work/output classes and available original PCs. Remaining
// unclosed semantic premises map to Unproved, never successful source.
RouteFailure FamilyRouteFailure(const FunctionTreeFailure& failure);
RouteFailure FamilyRouteFailure(const EvaluationFailure& failure);
RouteFailure FamilyRouteFailure(const FamilyPrepareFailure& failure);
RouteFailure FamilyRouteFailure(const FamilyEmitFailure& failure);
RouteFailure FamilyRouteFailure(const analysis::FunctionSiteFailure& failure);
}  // namespace abcd::source
#endif
