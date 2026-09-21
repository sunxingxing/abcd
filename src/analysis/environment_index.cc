#include "abcd/analysis/environment_index.h"

#include <new>

namespace abcd::analysis {
namespace {
std::variant<EnvironmentRef, EnvironmentError> CapturedAncestor(
    CapturedDepth capture, std::uint32_t distance) {
  if (distance > UINT32_MAX - capture.value)
    return EnvironmentError::kDepthRange;
  return EnvironmentRef{CapturedDepth{capture.value + distance}};
}
}  // namespace

std::variant<EnvironmentIndex, EnvironmentError> EnvironmentIndex::Build(
    std::span<const EnvironmentDefinition> definitions,
    EnvironmentBudget budget) {
  const auto count = definitions.size();
  if (count > UINT32_MAX || count > budget.bytes / sizeof(Node))
    return EnvironmentError::kResourceLimit;
  for (std::size_t i = 0; i < count; ++i) {
    if (const auto* parent = std::get_if<EnvironmentId>(&definitions[i].parent))
      if (parent->value >= i) return EnvironmentError::kInvalidParent;
  }
  try {
    EnvironmentIndex index;
    if (count != 0) index.nodes_ = std::make_unique<Node[]>(count);
    index.size_ = count;
    for (std::size_t i = 0; i < count; ++i) {
      auto& node = index.nodes_[i];
      node.definition = definitions[i];
      if (const auto* capture =
              std::get_if<CapturedDepth>(&definitions[i].parent)) {
        node.capture = *capture;
        node.height = 1;
        continue;
      }
      const auto parent_id = std::get<EnvironmentId>(definitions[i].parent);
      const auto& parent = index.nodes_[parent_id.value];
      node.capture = parent.capture;
      node.height = parent.height + 1;
      node.ancestors[0] = parent_id;
      // Two 2^(level-1) jumps make one 2^level jump. Absence means the
      // captured chain, not environment zero. External depths are added only
      // at query time, so unused large jumps cannot cause false overflow.
      for (std::size_t level = 1; level < kAncestorLevels; ++level) {
        const auto half = node.ancestors[level - 1];
        if (!half) break;
        node.ancestors[level] = index.nodes_[half->value].ancestors[level - 1];
      }
    }
    return index;
  } catch (const std::bad_alloc&) {
    return EnvironmentError::kResourceLimit;
  }
}

std::variant<EnvironmentRef, EnvironmentError> EnvironmentIndex::Ancestor(
    EnvironmentRef current, LexicalDepth depth) const {
  return AncestorAt(current, {depth.value});
}

std::variant<EnvironmentRef, EnvironmentError> EnvironmentIndex::AncestorAt(
    EnvironmentRef current, CapturedDepth depth) const {
  if (const auto* capture = std::get_if<CapturedDepth>(&current))
    return CapturedAncestor(*capture, depth.value);
  auto id = std::get<EnvironmentId>(current);
  if (id.value >= Size()) return EnvironmentError::kInvalidEnvironment;
  const auto& node = nodes_[id.value];
  std::uint32_t distance = depth.value;
  if (distance >= node.height)
    return CapturedAncestor(node.capture, distance - node.height);
  for (std::size_t level = 0; distance != 0; ++level, distance >>= 1) {
    if ((distance & 1) == 0) continue;
    const auto ancestor = nodes_[id.value].ancestors[level];
    if (!ancestor) return EnvironmentError::kInvalidEnvironment;
    id = *ancestor;
  }
  return EnvironmentRef{id};
}

std::variant<LexicalBinding, EnvironmentError> EnvironmentIndex::Resolve(
    EnvironmentRef current, LexicalAddress address) const {
  return ResolveCapture(current, {address.depth.value}, address.slot);
}

std::variant<LexicalBinding, EnvironmentError> EnvironmentIndex::ResolveCapture(
    EnvironmentRef current, CapturedDepth depth, LexicalSlot slot) const {
  const auto resolved = AncestorAt(current, depth);
  if (const auto* error = std::get_if<EnvironmentError>(&resolved))
    return *error;
  const auto environment = std::get<EnvironmentRef>(resolved);
  if (const auto* local = std::get_if<EnvironmentId>(&environment))
    if (slot.value >= nodes_[local->value].definition.allocation.slots)
      return EnvironmentError::kSlotRange;
  return LexicalBinding{environment, slot};
}

const EnvironmentDefinition* EnvironmentIndex::Find(EnvironmentId id) const {
  return id.value < Size() ? &nodes_[id.value].definition : nullptr;
}
std::size_t EnvironmentIndex::Size() const {
  // Default moves transfer the allocation. A moved-from index has no local
  // identities even though its scalar size member may retain the old value.
  return nodes_ ? size_ : 0;
}
std::size_t EnvironmentIndex::AllocatedBytes() const {
  return Size() * sizeof(Node);
}
}  // namespace abcd::analysis
