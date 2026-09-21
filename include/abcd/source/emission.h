#ifndef ABCD_SOURCE_EMISSION_H_
#define ABCD_SOURCE_EMISSION_H_

#include <cstddef>

namespace abcd::source {
enum class EmitError {
  kOutputLimit,
  kWrite,
  kInvalidBinding,
  kInvalidExpression,
  kNaNPayload
};
struct EmitBudget {
  std::size_t bytes = 64 * 1024 * 1024;
};
}  // namespace abcd::source
#endif
