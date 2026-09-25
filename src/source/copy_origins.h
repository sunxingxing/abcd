#ifndef ABCD_SOURCE_COPY_ORIGINS_H_
#define ABCD_SOURCE_COPY_ORIGINS_H_

#include <vector>

#include "abcd/analysis/register_ssa.h"

namespace abcd::source {
struct AstMethod;
// Exact value identity through already visited scalar slot copies only.
// Phi/entry values retain distinct identities; no heap alias or escape proof.
class CopyOrigins {
 public:
  explicit CopyOrigins(std::size_t values);
  void Observe(const AstMethod& method, analysis::InstructionId instruction);
  analysis::SsaValueId At(analysis::SsaValueId value) const;
  std::size_t AllocatedBytes() const;

 private:
  std::vector<analysis::SsaValueId> origins_;
};
}  // namespace abcd::source
#endif  // ABCD_SOURCE_COPY_ORIGINS_H_
