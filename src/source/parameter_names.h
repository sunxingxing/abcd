#ifndef ABCD_SOURCE_PARAMETER_NAMES_H_
#define ABCD_SOURCE_PARAMETER_NAMES_H_

#include <string>
#include <vector>

#include "abcd/abc/debug_scopes.h"
#include "abcd/abc/frame_layout.h"
#include "abcd/abc/method_body.h"
#include "abcd/abc/string_entity.h"

namespace abcd::source {
struct RecoveredParameters {
  std::vector<std::string> names;
  [[nodiscard]] std::size_t AllocatedBytes() const;
};
enum class ParameterNameError { kUnproven, kResourceLimit };
using ParameterOffsets = std::vector<abc::EntityOffset>;
using ParameterOffsetResult =
    std::variant<ParameterOffsets, ParameterNameError>;
using ParameterNameResult =
    std::variant<RecoveredParameters, ParameterNameError, abc::MetadataError>;

// Maps names only when the register at a real debug-range boundary is the
// original entry parameter through common copy/phi agreement. Exceptional
// Before/Commit/Catch timing is retained. Debug text supplies no types or
// storage identity; ambiguous evidence keeps synthesized parameter names.
[[nodiscard]] ParameterOffsetResult MapParameterDebugNames(
    const abc::FrameLayout& frame, const abc::MethodBody& body,
    const abc::DebugScopes& scopes, abc::ProfileId profile, std::size_t bytes);
[[nodiscard]] ParameterNameResult RecoverParameterNames(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MethodInfo& method, const abc::FrameLayout& frame,
    const abc::MethodBody& body, std::size_t bytes,
    abc::StringBudget strings = {}, std::string_view self_binding = {});
}  // namespace abcd::source
#endif  // ABCD_SOURCE_PARAMETER_NAMES_H_
