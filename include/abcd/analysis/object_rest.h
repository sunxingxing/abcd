#ifndef ABCD_ANALYSIS_OBJECT_REST_H_
#define ABCD_ANALYSIS_OBJECT_REST_H_
#include "abcd/analysis/register_effect.h"
namespace abcd::analysis {
// OH7 interpreter-inl.cpp:4149; runtime_stubs-inl.h:2624. count encodes
// keys-1; a first Undefined key means the empty list (checked by source proof).
struct ObjectRest {
  abc::RegisterId object, first;
  std::uint32_t count;
};
[[nodiscard]] std::variant<ObjectRest, ScalarError> LiftObjectRest(
    const abc::DecodedInstruction&, std::uint64_t frame);
}  // namespace abcd::analysis
#endif
