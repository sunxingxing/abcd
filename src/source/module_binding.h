#ifndef ABCD_SOURCE_MODULE_BINDING_H_
#define ABCD_SOURCE_MODULE_BINDING_H_

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include "abcd/source/local_lexical_body.h"

namespace abcd::source {
struct GuardedImports {
  std::vector<std::uint8_t> admitted;
  std::uint64_t work_units = 0;
  std::size_t AllocatedBytes() const { return admitted.capacity(); }
};

// A module load is sourceable only when an adjacent static-name Hole guard
// forces TDZ behavior. The caller owns the same-record module for the full
// function task; the generated binding must be declared in its module prelude.
[[nodiscard]] std::variant<GuardedImports, BodyEmitFailure>
ValidateGuardedImports(const abc::MethodBody& body,
                       const MethodConstants* constants, std::size_t bytes,
                       std::uint64_t work_limit);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_MODULE_BINDING_H_
