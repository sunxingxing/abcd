#include "family_route_failure.h"

#include <type_traits>

namespace abcd::source {
namespace {
template<class T> RouteFailureClass Classify(T error) {
  static_assert(std::is_enum_v<T>);
  if constexpr (requires { T::kResourceLimit; })
    if (error == T::kResourceLimit) return RouteFailureClass::kMemoryLimit;
  if constexpr (requires { T::kWorkLimit; })
    if (error == T::kWorkLimit) return RouteFailureClass::kWorkLimit;
  if constexpr (requires { T::kOutputLimit; })
    if (error == T::kOutputLimit) return RouteFailureClass::kOutputFailure;
  if constexpr (requires { T::kWrite; })
    if (error == T::kWrite) return RouteFailureClass::kOutputFailure;
  if constexpr (requires { T::kFileChanged; })
    if (error == T::kFileChanged) return RouteFailureClass::kInvalidInput;
  if constexpr (requires { T::kChanged; })
    if (error == T::kChanged) return RouteFailureClass::kInvalidInput;
  if constexpr (std::is_same_v<T, abc::BodyError>) return RouteFailureClass::kInvalidInput;
  if constexpr (std::is_same_v<T, FamilyEmitError>) return RouteFailureClass::kInvariantViolation;
  return RouteFailureClass::kUnproved;
}
template<class T> RouteFailure Project(const T& error, ValueRouteError stage) {
  if constexpr (std::is_same_v<T, RouteFailure>) return error;
  else if constexpr (std::is_enum_v<T>) return {Classify(error), stage, {}};
  else if constexpr (std::is_same_v<T, std::monostate>) return {RouteFailureClass::kUnproved, stage, {}};
  else if constexpr (requires { std::variant_size<T>::value; }) {
    return std::visit([&](const auto& nested) { return Project(nested, stage); }, error);
  } else {
    RouteFailure result{RouteFailureClass::kUnproved, stage, {}};
    if constexpr (requires { error.kind; }) result = Project(error.kind, stage);
    const auto merge = [&](const auto& nested) {
      const auto child = Project(nested, stage);
      if (result.failure_class == RouteFailureClass::kUnproved ||
          child.failure_class == RouteFailureClass::kMemoryLimit ||
          child.failure_class == RouteFailureClass::kWorkLimit ||
          child.failure_class == RouteFailureClass::kOutputFailure) result = child;
    };
    // Typed error envelopes only. New shapes must join at compile time;
    // no parsing of opcode names, diagnostic messages or generated source.
    if constexpr (requires { error.cause; }) merge(error.cause);
    else if constexpr (requires { error.error; }) merge(error.error);
    else if constexpr (requires { error.reason; }) merge(error.reason);
    else static_assert(requires { error.kind; });
    if constexpr (std::is_same_v<T, analysis::SemanticFailure>) merge(error.ssa_cause);
    if constexpr (std::is_same_v<T, analysis::RegisterUseFailure>)
      if (error.instruction) merge(*error.instruction);
    if constexpr (std::is_same_v<T, abc::BodyFailure>) {
      if (error.instruction_error) merge(*error.instruction_error);
      if (error.file_error) merge(*error.file_error);
    }
    if constexpr (std::is_same_v<T, BodyEmitFailure>) {
      if (error.scalar_error) merge(*error.scalar_error);
      if (error.emit_error) merge(*error.emit_error);
      if (error.metadata_error) merge(*error.metadata_error);
      if (error.literal_error) merge(*error.literal_error);
      if (error.object_error) merge(*error.object_error);
    }
    // Prefer the deepest PC; parent creation PCs are context, not child sites.
    if (!result.pc) {
      if constexpr (requires { std::optional<abc::CodeOffset>{error.pc}; }) result.pc = error.pc;
      else if constexpr (requires { std::optional<abc::CodeOffset>{error.offset}; }) result.pc = error.offset;
    }
    if constexpr (std::is_same_v<T, FamilyFileFailure>) if (error.family_creation) {
      result.family_creation = error.family_creation;
      result.method = error.family_creation->location.method;
      result.pc = error.family_creation->location.pc;
      result.working_pc = error.family_creation->location.working_pc;
    }
    if constexpr (std::is_same_v<T, AsyncCompletionFailure>) {
      result.async = error.async; result.method = error.async.location.method;
      result.pc = error.async.location.pc; result.working_pc = error.async.location.working_pc;
      result.failure_class = Classify(error.kind);
    }
    if constexpr (std::is_same_v<T, analysis::SemanticProofFailure>) result.internal_value = error.internal_value;
    if constexpr (std::is_same_v<T, CaptureFailure>) {
      result.capture = error.occurrence;
      if (error.occurrence && error.occurrence->source) {
        result.method = error.occurrence->source->method;
        result.pc = error.occurrence->source->pc;
        result.working_pc = error.occurrence->source->working_pc;
      }
    }
    if constexpr (std::is_same_v<T, ControlFailure>) {
      result.control = error.control;
      if (error.control.decision) {
        result.pc = error.control.node.pc; result.working_pc = error.control.node.working_pc;
      }
    }
    if constexpr (std::is_same_v<T, TreePrintFailure>) { result.printing=error;result.method=error.location.method;result.pc=error.location.pc;result.working_pc=error.location.working_pc; }
    if constexpr (std::is_same_v<T, ExceptionPlanFailure> || std::is_same_v<T, ExceptionControlRegionFailure>) result.exception_region = error.exception_region;
    if constexpr (std::is_same_v<T, CallPlanFailure>) result.call_reference = error.reference;
    if constexpr (std::is_same_v<T, PhasedFlowFailure> || std::is_same_v<T, PhasedControlFailure>) {
      result.method = error.method; result.pc = error.pc; result.working_pc = error.working_pc;
    }
    if constexpr (std::is_same_v<T, EvaluationFailure>) {
      result.evaluation = error.evaluation;
      result.method = error.evaluation.location.method;
      result.pc = error.evaluation.location.pc; result.working_pc = error.evaluation.location.working_pc;
    }
    if constexpr (requires { error.tree; }) if (error.tree) {
      result.tree = error.tree;
      result.method = error.tree->location.method;
      result.pc = error.tree->location.pc; result.working_pc = error.tree->location.working_pc;
    }
    if constexpr (requires { error.deferred; }) if (error.deferred) {
      result.deferred = error.deferred;
      result.method = error.deferred->creator;
      result.pc = error.deferred->creation.pc;
      result.working_pc = error.deferred->creation.working_pc;
    }
    if constexpr (requires { error.working_pc; }) result.working_pc = error.working_pc;
    if constexpr (requires { error.method; }) if (!result.method) result.method = error.method;
    if constexpr (requires { error.creation_pc; error.parent; }) {
      result.creation_pc = error.creation_pc;
      result.creation_method = error.parent;
    }
    if (result.evaluation && !result.evaluation->location.method) result.evaluation->location.method = result.method;
    // Newly exposed tree-verifier leaves are diagnostic. Their historical
    // outer classification remains authoritative (including completion's
    // TreeFlow catch); expression causes retain their pre-existing policy.
    // Print causes are diagnostic; preserve the former enum-only class.
    if constexpr (std::is_same_v<T, TreePrintFailure>) result.failure_class=Classify(error.kind);
    if constexpr (std::is_same_v<T, FunctionTreeFailure>)
      if (!std::holds_alternative<ExpressionFailure>(error.cause)) result.failure_class = Classify(error.kind);
    return result;
  }
}
template<class T> RouteFailure ProjectWithCauses(const T& error, ValueRouteError stage) {
  auto result = Project(error, stage);
  CollectFailureCauses(error, result.causes);
  return result;
}
}  // namespace
RouteFailure FamilyRouteFailure(const EvaluationFailure& failure) { return ProjectWithCauses(failure, ValueRouteError::kTreeStage); }
RouteFailure FamilyRouteFailure(const FunctionTreeFailure& failure) { return ProjectWithCauses(failure, ValueRouteError::kTreeStage); }
RouteFailure FamilyRouteFailure(const FamilyPrepareFailure& failure) { return ProjectWithCauses(failure, ValueRouteError::kTreeStage); }
RouteFailure FamilyRouteFailure(const FamilyEmitFailure& failure) { return ProjectWithCauses(failure, ValueRouteError::kPrintStage); }
RouteFailure FamilyRouteFailure(const analysis::FunctionSiteFailure& failure) { return ProjectWithCauses(failure, ValueRouteError::kContextStage); }
}  // namespace abcd::source
