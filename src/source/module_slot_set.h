#ifndef ABCD_SOURCE_MODULE_SLOT_SET_H_
#define ABCD_SOURCE_MODULE_SLOT_SET_H_

#include <memory>
#include <limits>
#include "abcd/analysis/module_load.h"

namespace abcd::source {
// Persistent membership only, never a value/identity snapshot. Slot width is
// the pinned ModuleSlot wire type; inserts share all unchanged trie branches.
class ModuleSlotSet {
 public:
  // One bounded root-to-leaf path, including the leaf. Admission callers charge
  // this for lookup/insertion work independently of the number of stored slots.
  static constexpr unsigned kPathWork =
      std::numeric_limits<decltype(analysis::ModuleSlot::value)>::digits + 1;
  bool Contains(analysis::ModuleSlot slot) const;
  bool Empty() const { return !root_; }
  bool Same(const ModuleSlotSet& other) const { return root_ == other.root_; }
  ModuleSlotSet With(analysis::ModuleSlot slot, std::size_t& bytes) const;
 private:
  struct Node;
  static std::shared_ptr<const Node> Insert(std::shared_ptr<const Node>,
      std::uint16_t slot, unsigned bits);
  std::shared_ptr<const Node> root_;
};
}
#endif
