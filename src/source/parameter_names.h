#ifndef ABCD_SOURCE_PARAMETER_NAMES_H_
#define ABCD_SOURCE_PARAMETER_NAMES_H_

#include <optional>
#include <string>
#include <vector>

#include "abcd/abc/debug_scopes.h"
#include "abcd/abc/frame_layout.h"
#include "abcd/abc/method_body.h"
#include "abcd/abc/string_entity.h"

namespace abcd::source {
struct RecoveredParameters {
  // ABI ordinals stay fixed even when only some debug observations are proven.
  std::vector<std::optional<std::string>> names;
  [[nodiscard]] std::size_t AllocatedBytes() const;
};
// Legacy writers reserve private textual namespaces. SourceTree owns generated
// bindings and proves actual shadowing; those exclusions are not JS syntax.
enum class ParameterNameDomain { kLegacyWriter, kSourceTree };
enum class ParameterNameError { kUnproven, kResourceLimit };
using ParameterOffsets = std::vector<std::optional<abc::EntityOffset>>;
using ParameterOffsetResult =
    std::variant<ParameterOffsets, ParameterNameError>;
using ParameterNameResult =
    std::variant<RecoveredParameters, ParameterNameError, abc::MetadataError>;

// Maps names only when the register at a real debug-range boundary is the
// original entry parameter through common copy/phi agreement. Exceptional
// Before/Commit/Catch timing is retained. Debug text supplies no types or
// storage identity. SourceTree keeps each unique name independently; missing,
// conflicting or unsafe names stay synthesized. LegacyWriter requires the
// complete original set at its separate text boundary.
[[nodiscard]] ParameterOffsetResult MapParameterDebugNames(
    const abc::FrameLayout& frame, const abc::MethodBody& body,
    const abc::DebugScopes& scopes, abc::ProfileId profile, std::size_t bytes,
    ParameterNameDomain domain = ParameterNameDomain::kLegacyWriter);
[[nodiscard]] ParameterNameResult RecoverParameterNames(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MethodInfo& method, const abc::FrameLayout& frame,
    const abc::MethodBody& body, std::size_t bytes,
    abc::StringBudget strings = {}, std::string_view self_binding = {},
    ParameterNameDomain domain = ParameterNameDomain::kLegacyWriter);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_PARAMETER_NAMES_H_
