#ifndef ABCD_SOURCE_FUNCTION_EMIT_ERROR_H_
#define ABCD_SOURCE_FUNCTION_EMIT_ERROR_H_

namespace abcd::source {
// Typed callable-envelope failures of value-tree function and class emission.
enum class FunctionEmitError {
  kCreation,
  kKind,
  kSendable,
  kFrame,
  kLength,
  kName,
  kResourceLimit,
  kSuperclass
};
}  // namespace abcd::source
#endif  // ABCD_SOURCE_FUNCTION_EMIT_ERROR_H_
