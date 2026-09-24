#ifndef ABCD_SOURCE_SEMANTIC_OBJECTS_H_
#define ABCD_SOURCE_SEMANTIC_OBJECTS_H_

#include "abcd/analysis/semantic_ir.h"

namespace abcd::source {
// Same-block allocation identities, not heap-shape or arbitrary alias facts.
// A possible publication invalidates all older objects, including containers.
class PrivateObjects {
 public:
  using Origin = std::optional<analysis::InstructionId>;
  explicit PrivateObjects(std::size_t values) : origins_(values) {}
  std::size_t AllocatedBytes() const {
    return origins_.capacity() * sizeof(Origin);
  }
  Origin Find(std::optional<analysis::SsaValueId> value,
              std::size_t block_begin) const;
  void Observe(const analysis::SemanticMethod& method,
               analysis::InstructionId instruction, std::size_t block_begin,
               bool verified_own_store);

 private:
  std::vector<Origin> origins_;
  Origin publication_;
};
}  // namespace abcd::source
#endif  // ABCD_SOURCE_SEMANTIC_OBJECTS_H_
