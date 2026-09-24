#ifndef ABCD_SOURCE_SEMANTIC_OBJECTS_H_
#define ABCD_SOURCE_SEMANTIC_OBJECTS_H_

#include "abcd/analysis/semantic_ir.h"

namespace abcd::source {
struct AstMethod;
// Same-block allocation identities, not heap-shape or arbitrary alias facts.
// Publication invalidates the connected component, including containers.
class PrivateObjects {
  struct Allocation {
    std::size_t parent = 0;
    std::uint8_t rank = 0;
    bool escaped = false;
  };
 public:
  using Origin = std::optional<analysis::InstructionId>;
  static constexpr std::size_t kAllocationBytes = sizeof(Allocation);
  PrivateObjects(std::size_t values, std::size_t instructions)
      : origins_(values), allocations_(instructions) {
    for (std::size_t i = 0; i < instructions; ++i) allocations_[i].parent = i;
  }
  std::size_t AllocatedBytes() const {
    return origins_.capacity() * sizeof(Origin) +
           allocations_.capacity() * sizeof(Allocation);
  }
  Origin Find(std::optional<analysis::SsaValueId> value,
              std::size_t block_begin) const;
  void Observe(const AstMethod& method,
               analysis::InstructionId instruction, std::size_t block_begin,
               bool verified_own_store);

 private:
  Origin Identity(analysis::SsaValueId value, std::size_t block_begin) const;
  std::size_t Root(std::size_t id) const;
  void Connect(analysis::InstructionId left, analysis::InstructionId right);
  std::vector<Origin> origins_;
  std::vector<Allocation> allocations_;
};
}  // namespace abcd::source
#endif  // ABCD_SOURCE_SEMANTIC_OBJECTS_H_
