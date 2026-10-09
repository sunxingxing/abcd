#ifndef ABCD_SOURCE_RECOVERY_SCRATCH_H_
#define ABCD_SOURCE_RECOVERY_SCRATCH_H_
#include "tree_checks.h"
#include <memory_resource>
#include <memory>
namespace abcd::source {
// Immutable shared owners debit the allocator's actual rebound control block.
// Admission is borrowed only while constructing; destruction never reads it.
template<class T> struct RecoveryAllocator {
  using value_type=T;
  TreeCheckBudget* admission;
  explicit RecoveryAllocator(TreeCheckBudget& budget):admission(&budget){}
  template<class U> RecoveryAllocator(const RecoveryAllocator<U>& other):admission(other.admission){}
  T* allocate(std::size_t count) {admission->Admit(count,sizeof(T));return std::allocator<T>{}.allocate(count);}
  void deallocate(T* memory,std::size_t count) noexcept {std::allocator<T>{}.deallocate(memory,count);}
  template<class U> bool operator==(const RecoveryAllocator<U>&)const noexcept{return true;}
};
// Charge cumulative growth, including overlap during container reallocation.
// This conservative admission is not a process peak-RSS measurement.
class RecoveryScratch final : public std::pmr::memory_resource {
 public:
  explicit RecoveryScratch(TreeCheckBudget& budget) : budget_(budget) {}
 private:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    budget_.Admit(bytes, 1);
    return std::pmr::new_delete_resource()->allocate(bytes, alignment);
  }
  void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override {
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
  TreeCheckBudget& budget_;
};
}
#endif
