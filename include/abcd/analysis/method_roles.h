#ifndef ABCD_ANALYSIS_METHOD_ROLES_H_
#define ABCD_ANALYSIS_METHOD_ROLES_H_
#include "abcd/abc/literal_array.h"
#include "abcd/abc/method_body.h"
#include "abcd/abc/method_target.h"

namespace abcd::analysis {
enum class MethodRole : std::uint8_t { kUnseen, kOrdinary, kOther, kConflict };
enum class MethodRoleError { kResourceLimit, kLegacyLiteral, kNestedLiteral };
struct MethodRoleFailure {
  std::variant<MethodRoleError, abc::MetadataError, abc::BodyFailure,
               abc::LiteralFailure, abc::MethodTargetError>
      error;
};
struct MethodRoleBudget {
  std::size_t bytes = 64 * 1024 * 1024;
};
// Whole-file, immutable index of constructor uses and method-bearing literal
// tags referenced by decoded methods, including object/array/class buffers.
// kOrdinary excludes all other template kinds AND constructor/JIT roles,
// independent of materialization order. Does not prove initial attributes,
// class membership, historical runtime equivalence or name. Requires complete
// same-file metadata/lookup and pinned modern 7.0 semantics;
// legacy/nested/unreadable literals fail the whole proof, never disappear.
// Two bounded body passes, then sort/deduplicate K literal references. With M
// methods, C constructor references and L deduplicated literal entries, time is
// O(M + decoded bytes + K log K + (K+C) log regions + (L+C) log M).
// Payload: one byte/method + (K+C) offsets + one body OR literal scratch.
// Input index/lookup/file and allocator overhead are caller-admitted.
class MethodRoleIndex {
 public:
  MethodRoleIndex(MethodRoleIndex&&) noexcept = default;
  MethodRoleIndex& operator=(MethodRoleIndex&&) noexcept = default;
  [[nodiscard]] static std::variant<MethodRoleIndex, MethodRoleFailure> Build(
      const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
      MethodRoleBudget budget = {});
  [[nodiscard]] std::optional<MethodRole> At(
      abc::MethodPosition position) const {
    if (!roles_ || position.value >= count_) return {};
    return roles_[position.value];
  }
  [[nodiscard]] std::size_t AllocatedBytes() const {
    return roles_ ? count_ * sizeof(MethodRole) : 0;
  }

 private:
  MethodRoleIndex() = default;
  std::unique_ptr<MethodRole[]> roles_;
  std::size_t count_ = 0;
};
}  // namespace abcd::analysis
#endif
