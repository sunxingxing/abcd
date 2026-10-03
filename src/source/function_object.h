#ifndef ABCD_SOURCE_FUNCTION_OBJECT_H_
#define ABCD_SOURCE_FUNCTION_OBJECT_H_

#include "abcd/analysis/value_ir.h"

namespace abcd::source {
enum class FunctionObjectKind : std::uint8_t { kClass, kFunction };
// OH7 RuntimeDefinefunc and class creation allocate direct JSFunction objects.
// Their identity cannot become a Proxy through property/prototype mutation.
// This says nothing about IsConstructor, the target body, or captures: normal
// native `new` and JSFunction::ConstructInternal both check constructability
// after argument evaluation; family/class publication must still prove syntax.
// Never infer this brand from parameters, names, getter results or wrappers.
[[nodiscard]] inline std::optional<FunctionObjectKind> DirectFunctionObject(
    const analysis::ValueOperation& operation) {
  if (std::holds_alternative<analysis::ValueClassCreation>(operation)) return FunctionObjectKind::kClass;
  if (const auto* function = std::get_if<analysis::ValueFunctionCreation>(&operation);
      function && function->kind == analysis::CreationKind::kFunction &&
      function->home_object == analysis::HomeObjectInput::kCurrentFunction)
    return FunctionObjectKind::kFunction;
  return {};
}
}  // namespace abcd::source
#endif
