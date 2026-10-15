#ifndef ABCD_ANALYSIS_CLASS_CREATION_H_
#define ABCD_ANALYSIS_CLASS_CREATION_H_
#include "abcd/abc/register_access.h"
#include "abcd/abc/class_kind.h"
#include "abcd/analysis/function_creation.h"
namespace abcd::analysis {
struct ClassCreation {
  abc::PoolIndex constructor;
  abc::PoolIndex literal;
  std::uint16_t length = 0;
  abc::RegisterId parent;
  std::uint16_t cache_slot = 0;
  abc::ClassKind kind = abc::ClassKind::kOrdinary;
  bool operator==(const ClassCreation&) const = default;
};
// Modern class site. Parent register is not a proven extends
// expression. Constructor/member ownership is per dynamic creation occurrence.
[[nodiscard]] std::variant<ClassCreation, CreationError> LiftClassCreation(
    const abc::DecodedInstruction& instruction);
}  // namespace abcd::analysis
#endif
