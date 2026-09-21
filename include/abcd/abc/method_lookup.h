#ifndef ABCD_ABC_METHOD_LOOKUP_H_
#define ABCD_ABC_METHOD_LOOKUP_H_

#include <memory>
#include <span>

#include "abcd/abc/metadata_index.h"

namespace abcd::abc {
struct MethodPosition {
  std::uint32_t value = 0;
  auto operator<=>(const MethodPosition&) const = default;
};
enum class MethodLookupError { kDuplicateOffset, kResourceLimit };
struct MethodLookupBudget {
  std::size_t bytes = 64 * 1024 * 1024;
};

// Immutable entity-membership index, not a closure-instance identity table.
// Positions refer to the unchanged source method vector. Build once per file;
// no per-reference table scan or copying of names/body metadata. The caller
// must not mix file/index contexts. Payload is admitted before allocation;
// caller metadata and allocator overhead are outside this budget.
class MethodLookup {
 public:
  MethodLookup(MethodLookup&&) noexcept = default;
  MethodLookup& operator=(MethodLookup&&) noexcept = default;
  [[nodiscard]] static std::variant<MethodLookup, MethodLookupError> Build(
      std::span<const MethodInfo> methods, MethodLookupBudget budget = {});
  [[nodiscard]] std::optional<MethodPosition> Find(EntityOffset offset) const;
  [[nodiscard]] std::size_t Size() const { return entries_ ? size_ : 0; }
  [[nodiscard]] std::size_t AllocatedBytes() const {
    return Size() * sizeof(Entry);
  }

 private:
  struct Entry {
    EntityOffset offset;
    MethodPosition position;
  };
  MethodLookup() = default;
  std::unique_ptr<Entry[]> entries_;
  std::size_t size_ = 0;
};
}  // namespace abcd::abc
#endif  // ABCD_ABC_METHOD_LOOKUP_H_
