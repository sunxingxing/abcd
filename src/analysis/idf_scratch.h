#ifndef ABCD_ANALYSIS_IDF_SCRATCH_H_
#define ABCD_ANALYSIS_IDF_SCRATCH_H_

#include <algorithm>
#include <memory_resource>
#include <new>

namespace abcd::analysis::detail {
// Query-local resource. Counts overlapping growth buffers; no allocation using
// this resource or pointer to it may escape the query's owned result boundary.
class IdfScratch final : public std::pmr::memory_resource {
 public:
  explicit IdfScratch(std::size_t limit) : limit_(limit) {}
  std::size_t Live() const { return live_; }
  std::size_t Peak() const { return peak_; }

 private:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    if (bytes > limit_ - live_) throw std::bad_alloc{};
    auto* memory = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    live_ += bytes;
    peak_ = std::max(peak_, live_);
    return memory;
  }
  void do_deallocate(void* memory, std::size_t bytes,
                     std::size_t alignment) override {
    std::pmr::new_delete_resource()->deallocate(memory, bytes, alignment);
    live_ -= bytes;
  }
  bool do_is_equal(
      const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }
  std::size_t limit_;
  std::size_t live_ = 0;
  std::size_t peak_ = 0;
};
struct IdfWorkLimit {};
}  // namespace abcd::analysis::detail
#endif  // ABCD_ANALYSIS_IDF_SCRATCH_H_
