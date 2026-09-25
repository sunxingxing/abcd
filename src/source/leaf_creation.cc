#include "leaf_creation.h"

#include <type_traits>

#include "abcd/source/created_function.h"
#include "function_template.h"

namespace abcd::source {
namespace {
BodyEmitResult CreationResult(const CreatedFunctionResult& result,
                              abc::CodeOffset pc) {
  return std::visit(
      [&](auto value) -> BodyEmitResult {
        using T = decltype(value);
        if constexpr (std::is_same_v<T, std::size_t>) {
          return value;
        } else {
          BodyEmitFailure failure{
              BodyEmitError::kCreation, pc, {}, {}};
          bool resource = false;
          if constexpr (std::is_same_v<T, EmitError>) {
            failure.emit_error = value;
          } else if constexpr (std::is_same_v<T, FunctionEmitError>) {
            resource = value == FunctionEmitError::kResourceLimit;
          } else if constexpr (std::is_same_v<T, CreatedFunctionError>) {
            resource = value == CreatedFunctionError::kResourceLimit;
          } else if constexpr (std::is_same_v<T, abc::MetadataError>) {
            failure.metadata_error = value;
            resource = value == abc::MetadataError::kResourceLimit;
          } else if constexpr (std::is_same_v<T, abc::BodyFailure>) {
            resource = value.kind == abc::BodyError::kResourceLimit;
          } else if constexpr (std::is_same_v<T, analysis::FlowFailure>) {
            resource = value.kind == analysis::FlowError::kResourceLimit;
          } else if constexpr (std::is_same_v<T, analysis::AccumulatorFailure>) {
            resource = value.kind ==
                       analysis::AccumulatorError::kResourceLimit;
          } else if constexpr (std::is_same_v<T, AcyclicError>) {
            resource = value == AcyclicError::kResourceLimit;
          } else if constexpr (std::is_same_v<
                                   T, analysis::EnvironmentFlowFailure>) {
            resource =
                value.kind == analysis::EnvironmentFlowError::kResourceLimit;
          } else if constexpr (std::is_same_v<T,
                                              analysis::LinearLexicalFailure>) {
            resource =
                value.kind == analysis::LinearLexicalError::kResourceLimit;
            if (value.kind == analysis::LinearLexicalError::kWorkLimit)
              failure.kind = BodyEmitError::kWorkLimit;
          } else if constexpr (std::is_same_v<T, BodyEmitFailure>) {
            resource = value.kind == BodyEmitError::kResourceLimit;
            failure.emit_error = value.emit_error;
            failure.metadata_error = value.metadata_error;
            if (value.kind == BodyEmitError::kWorkLimit)
              failure.kind = BodyEmitError::kWorkLimit;
          } else {
            // RouteRefusal carries no body-failure detail and cannot occur in
            // this leaf path: it only ever runs inside an already admitted
            // legacy invocation (invocation-context rule in source_route.h).
            static_assert(std::is_same_v<T, analysis::CreationError> ||
                          std::is_same_v<T, abc::MethodTargetError> ||
                          std::is_same_v<T, abc::MethodAttributeError> ||
                          std::is_same_v<T, abc::FrameError> ||
                          std::is_same_v<T, RouteRefusal>);
          }
          if (resource) failure.kind = BodyEmitError::kResourceLimit;
          return failure;
        }
      },
      result);
}
}  // namespace
BodyEmitResult WriteLeafCreation(const abc::BodyInstruction& site,
                                 const MethodConstants& constants,
                                 const abc::MethodLookup& methods,
                                 std::ostream& output,
                                 LocalLexicalBudget budget) {
  return CreationResult(WriteCreatedFunctionTemplate(
      constants.file, constants.header, constants.index, methods,
      constants.method, site.decoded, output, budget, constants.string_budget,
      constants.module, constants.roles,
      constants.global_scope ? &*constants.global_scope : nullptr), site.offset);
}
BodyEmitResult WriteLeafFunction(const analysis::FunctionCreation& creation,
                                 abc::CodeOffset pc,
                                 const MethodConstants& constants,
                                 std::ostream& output,
                                 LocalLexicalBudget budget,
                                 analysis::CapturedValues captures,
                                 CreationExpansion* expansion) {
  if (!constants.leaf_methods)
    return BodyEmitFailure{BodyEmitError::kCreation, pc, {}, {}};
  if (captures == analysis::CapturedValues::kLanguage)
    return CreationResult(WriteCreatedFunctionTemplate(
        constants.file, constants.header, constants.index, *constants.leaf_methods,
        constants.method, creation, output, budget, constants.string_budget,
        constants.module, constants.roles,
        constants.global_scope ? &*constants.global_scope : nullptr, expansion), pc);
  return CreationResult(WriteCreatedFunction(
      constants.file, constants.header, constants.index, *constants.leaf_methods,
      constants.method, creation, output, budget, constants.string_budget,
      constants.module, constants.roles,
      constants.global_scope ? &*constants.global_scope : nullptr), pc);
}
}  // namespace abcd::source
