#ifndef ABCD_ANALYSIS_DETAIL_PREPAID_SHARED_H_
#define ABCD_ANALYSIS_DETAIL_PREPAID_SHARED_H_
#include <memory>
#include "abcd/analysis/detail/idf_scratch.h"

namespace abcd::analysis::detail {
// allocate_shared rebinds to its actual control block. Charge that allocation,
// rather than guessing implementation overhead. Deallocation never consults
// the query-local ledger: the immutable shared owner may outlive it.
template<class T> struct PrepaidSharedAllocator {
  using value_type = T;
  IdfScratch* memory;
  std::size_t* bytes;
  template<class U> PrepaidSharedAllocator(const PrepaidSharedAllocator<U>& other)
      : memory(other.memory), bytes(other.bytes) {}
  PrepaidSharedAllocator(IdfScratch& memory, std::size_t& bytes) : memory(&memory), bytes(&bytes) {}
  T* allocate(std::size_t count) {
    if(count > memory->Available()/sizeof(T)) throw std::bad_alloc{};
    memory->Retain(count*sizeof(T)); *bytes += count*sizeof(T);
    return std::allocator<T>{}.allocate(count);
  }
  void deallocate(T* ptr, std::size_t count) { std::allocator<T>{}.deallocate(ptr,count); }
  template<class U> bool operator==(const PrepaidSharedAllocator<U>& other) const { return memory==other.memory; }
};
}  // namespace abcd::analysis::detail
#endif
