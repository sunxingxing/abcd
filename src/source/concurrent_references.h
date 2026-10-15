#ifndef ABCD_SOURCE_CONCURRENT_REFERENCES_H_
#define ABCD_SOURCE_CONCURRENT_REFERENCES_H_
#include "abcd/source/source_tree.h"
namespace abcd::source {
// Consumes typed identifiers from the final serialization projection. The
// printer reports identity only; this proof never parses names or source text.
class ConcurrentReferenceProof {
 public:
  explicit ConcurrentReferenceProof(const ConcurrentDirective& input):input_(&input){}
  std::optional<ConcurrentFailure> Observe(ModuleSymbol);
  std::optional<ConcurrentFailure> Finish() const;
 private:
  const ConcurrentDirective* input_;
  std::size_t consumed_=0;
};
}
#endif
