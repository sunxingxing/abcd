#ifndef ABCD_ANALYSIS_PROPERTY_DELETE_H_
#define ABCD_ANALYSIS_PROPERTY_DELETE_H_
#include "abcd/analysis/scalar.h"
namespace abcd::analysis {
// Pinned RuntimeDelObjProp: ToObject, ToPropertyKey, DeletePropertyOrThrow.
// No GetValue of the property. Acc becomes boolean only after success.
struct PropertyDelete { abc::RegisterId object; };
[[nodiscard]] std::variant<PropertyDelete, ScalarError> LiftPropertyDelete(
    const abc::DecodedInstruction&, std::uint64_t frame_size);
}
#endif
