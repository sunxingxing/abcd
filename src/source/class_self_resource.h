#ifndef ABCD_SOURCE_CLASS_SELF_RESOURCE_H_
#define ABCD_SOURCE_CLASS_SELF_RESOURCE_H_
#include <variant>

#include "abcd/source/class_self_failure.h"
namespace abcd::source {
// Preserve nested resource causes. This adapter grants no semantic permission.
using ClassSelfResourceError = ClassSelfInitializationError;
template <class T>
std::optional<ClassSelfResourceError> ClassSelfResource(const T& value) {
  if constexpr (std::is_enum_v<T>) {
    if constexpr (requires { T::kResourceLimit; })
      if (value == T::kResourceLimit)
        return ClassSelfResourceError::kResourceLimit;
    if constexpr (requires { T::kMemoryLimit; })
      if (value == T::kMemoryLimit)
        return ClassSelfResourceError::kResourceLimit;
    if constexpr (requires { T::kWorkLimit; })
      if (value == T::kWorkLimit) return ClassSelfResourceError::kWorkLimit;
  } else if constexpr (requires { std::variant_size<T>::value; }) {
    return std::visit([](const auto& v) { return ClassSelfResource(v); },
                      value);
  } else {
    if constexpr (requires { value.failure_class; })
      if (const auto e = ClassSelfResource(value.failure_class)) return e;
    if constexpr (requires { value.kind; })
      if (const auto e = ClassSelfResource(value.kind)) return e;
    if constexpr (requires { value.cause; })
      if (const auto e = ClassSelfResource(value.cause)) return e;
    if constexpr (requires { value.error; })
      if (const auto e = ClassSelfResource(value.error)) return e;
    if constexpr (requires { value.environment_error; })
      if (value.environment_error)
        if (const auto e = ClassSelfResource(*value.environment_error))
          return e;
    if constexpr (requires { value.ssa_cause; })
      if (const auto e = ClassSelfResource(value.ssa_cause)) return e;
  }
  return {};
}
}  // namespace abcd::source
#endif
