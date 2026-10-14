#ifndef ABCD_SOURCE_FOR_IN_QUOTA_H_
#define ABCD_SOURCE_FOR_IN_QUOTA_H_
#include "for_in_plan.h"
#include "../analysis/idf_scratch.h"
#include <limits>

namespace abcd::source::detail {
struct ForInQuota {
  analysis::detail::IdfScratch memory;
  std::uint64_t remaining, initial;
  ForInFailure witness{ForInError::kCorrespondence};
  explicit ForInQuota(analysis::SemanticBudget budget)
      : memory(budget.bytes), remaining(budget.work), initial(budget.work) {}
  void Step(std::uint64_t count = 1) {
    if (count > remaining) Fail(ForInError::kWorkLimit);
    remaining -= count;
  }
  [[noreturn]] void Fail(ForInError kind) {
    witness.kind = kind; throw witness;
  }
  template<class T> void Require(const T& condition, ForInError kind = ForInError::kCorrespondence) {
    if (!condition) Fail(kind);
  }
  analysis::SemanticBudget Remaining() { return {memory.Available(), remaining}; }
  template<class T> void Reserve(std::vector<T>& values, std::size_t count) {
    Require(count <= UINT32_MAX && count <= memory.Available() / sizeof(T), ForInError::kResourceLimit);
    memory.Retain(count * sizeof(T));
    values.reserve(count);
    memory.Retain((values.capacity() - count) * sizeof(T));
  }
};
inline bool Inert(const analysis::ValueOp& op) {
  return op.normal == analysis::LanguageEffect::kNone &&
      op.before_throw == analysis::LanguageEffect::kNone &&
      op.exceptional == analysis::ExceptionalCompletion::kImpossible &&
      !std::holds_alternative<analysis::ValueReturn>(op.operation) &&
      !std::holds_alternative<analysis::ValueAwait>(op.operation) &&
      !std::holds_alternative<analysis::ValueYield>(op.operation);
}
}  // namespace abcd::source::detail
#endif
