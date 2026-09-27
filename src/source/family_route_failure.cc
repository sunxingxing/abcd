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
    return result;
  }
}
}  // namespace
RouteFailure FamilyRouteFailure(const FamilyPrepareFailure& failure) { return Project(failure, ValueRouteError::kTreeStage); }
RouteFailure FamilyRouteFailure(const FamilyEmitFailure& failure) { return Project(failure, ValueRouteError::kPrintStage); }
RouteFailure FamilyRouteFailure(const analysis::FunctionSiteFailure& failure) { return Project(failure, ValueRouteError::kContextStage); }
}  // namespace abcd::source
