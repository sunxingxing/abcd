#ifndef ABCD_SOURCE_MODULE_BINDING_H_
#define ABCD_SOURCE_MODULE_BINDING_H_

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include "abcd/source/local_lexical_body.h"

namespace abcd::source {
struct GuardedModuleLoads {
  std::vector<std::uint8_t> admitted;
  std::uint64_t work_units = 0;
  std::size_t AllocatedBytes() const { return admitted.capacity(); }
};

// A module load is sourceable only when an adjacent static-name Hole guard
// forces TDZ behavior. Local loads also require a caller-proven slot in the
// same record's live $local bindings; their declarations occur at first store.
[[nodiscard]] std::variant<GuardedModuleLoads, BodyEmitFailure>
ValidateGuardedModuleLoads(const abc::MethodBody& body,
                           const MethodConstants* constants,
                           std::size_t bytes, std::uint64_t work_limit);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_MODULE_BINDING_H_
