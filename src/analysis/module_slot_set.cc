#include "abcd/analysis/module_slot_set.h"

#include <limits>
#include <stdexcept>

namespace abcd::analysis {
namespace {
constexpr unsigned kSlotBits = ModuleSlotSet::kPathWork - 1;
// allocate_shared rebinds to the actual control-block allocation type. Charge
// that allocation, not a guessed ABI overhead. Allocators retained in shared
// control blocks are stateless: no quota pointer outlives this synchronous call.
thread_local std::size_t* slot_bytes = nullptr;
struct SlotAllocation {
  std::size_t* previous;
  explicit SlotAllocation(std::size_t& bytes) : previous(slot_bytes) { slot_bytes = &bytes; }
  ~SlotAllocation() { slot_bytes = previous; }
};
template<class T> struct SlotAllocator {
  using value_type = T;
  SlotAllocator() = default;
  template<class U> SlotAllocator(const SlotAllocator<U>&) {}
  T* allocate(std::size_t count) {
    if (!slot_bytes || count > *slot_bytes / sizeof(T)) throw std::length_error("module slot storage");
    *slot_bytes -= count * sizeof(T);
    return std::allocator<T>{}.allocate(count);
  }
  void deallocate(T* data, std::size_t count) { std::allocator<T>{}.deallocate(data, count); }
  template<class U> bool operator==(const SlotAllocator<U>&) const { return true; }
};
}
struct ModuleSlotSet::Node {
  std::shared_ptr<const Node> zero, one;
};
bool ModuleSlotSet::Contains(analysis::ModuleSlot slot) const {
  auto node = root_;
  for (unsigned bits = kSlotBits; node && bits; --bits)
    node = slot.value & (std::uint16_t{1} << (bits - 1)) ? node->one : node->zero;
  return node != nullptr;
}
std::shared_ptr<const ModuleSlotSet::Node> ModuleSlotSet::Insert(
    std::shared_ptr<const Node> old, std::uint16_t slot, unsigned bits) {
  if (!bits) return old ? old : std::allocate_shared<Node>(SlotAllocator<Node>{});
  const bool one = slot & (std::uint16_t{1} << (bits - 1));
  const auto prior = old ? (one ? old->one : old->zero) : nullptr;
  auto child = Insert(prior, slot, bits - 1);
  if (old && prior == child) return old;
  auto node = std::allocate_shared<Node>(SlotAllocator<Node>{});
  if (old) { node->zero = old->zero; node->one = old->one; }
  (one ? node->one : node->zero) = std::move(child);
  return node;
}
ModuleSlotSet ModuleSlotSet::With(analysis::ModuleSlot slot, std::size_t& bytes) const {
  const SlotAllocation allocation(bytes);
  ModuleSlotSet result;
  result.root_ = Insert(root_, slot.value, kSlotBits);
  return result;
}
}
