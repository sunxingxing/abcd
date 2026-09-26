#include "scope_facts.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <tuple>

namespace abcd::source {
std::uint64_t ScopeFacts::QueryWork() const {
  // Five binary searches plus the fixed ancestry walk and bounded field checks.
  constexpr auto kChecks = std::numeric_limits<std::uint32_t>::digits;
  return 2 * kChecks + std::bit_width(locals_.cells.size()) +
      std::bit_width(locals_.stores.size()) + std::bit_width(method_.flow.blocks.size()) +
      std::bit_width(locals_.capture_barriers.size()) + std::bit_width(defined_.guards.size());
}
bool ScopeFacts::Matches(analysis::InstructionId site,
    const analysis::CreationBinding& creation) const {
  const auto* ordinary = std::get_if<analysis::OrdinaryCapture>(&creation.capture);
  if (!ordinary || site.value >= method_.instructions.size()) return false;
  const auto* operation = std::get_if<analysis::FunctionCreation>(&method_.instructions[site.value].operation);
  return operation && operation->kind == creation.creation.kind &&
      operation->method == creation.creation.method &&
      operation->length.value == creation.creation.length.value &&
      operation->home_object == creation.creation.home_object &&
      operation->cache_slot == creation.creation.cache_slot && !creation.target.attributes.sendable &&
      site.value < environments_.before.size() && environments_.before[site.value] &&
      method_.instructions[site.value].pc == creation.offset &&
      *environments_.before[site.value] == ordinary->environment;
}
std::variant<CaptureCell, ScopeError> ScopeFacts::Rebase(
    analysis::InstructionId creation, analysis::CapturedSlot capture,
    analysis::CaptureDomain requirement) const {
  using Error = ScopeError;
  switch (requirement) {
    case analysis::CaptureDomain::kLanguage:
    case analysis::CaptureDomain::kDefined: break;
    default: return Error::kDomain;
  }
  const auto instruction = creation.value;
  if (instruction >= method_.instructions.size() || instruction >= environments_.before.size() ||
      !environments_.before[instruction]) return Error::kSite;
  const auto resolved = environments_.environments.ResolveCapture(
      *environments_.before[instruction], capture.depth, capture.slot);
  const auto* binding = std::get_if<analysis::LexicalBinding>(&resolved);
  if (!binding) return Error::kEnvironment;
  if (const auto* depth = std::get_if<analysis::CapturedDepth>(&binding->environment))
    return CaptureCell{analysis::CapturedSlot{*depth, binding->slot}};
  const auto* environment = std::get_if<analysis::EnvironmentId>(&binding->environment);
  if (!environment || environment->value >= locals_.allocations.size()) return Error::kCell;
  const analysis::LocalCell cell{*environment, binding->slot};
  const auto key = [](auto item) { return std::tuple(item.environment.value, item.slot.value); };
  const auto found = std::lower_bound(locals_.cells.begin(), locals_.cells.end(), cell,
      [&](auto a, auto b) { return key(a) < key(b); });
  if (found == locals_.cells.end() || key(*found) != key(cell)) return Error::kCell;
  const auto id = static_cast<std::uint32_t>(found - locals_.cells.begin());
  auto store = std::lower_bound(locals_.stores.begin(), locals_.stores.end(), std::tuple(id, instruction),
      [](auto item, auto target) { return std::tuple(item.cell.value, item.instruction.value) < target; });
  if (store == locals_.stores.begin() || (--store)->cell.value != id) return Error::kInitialization;
  const auto& blocks = method_.flow.blocks;
  auto block = std::upper_bound(blocks.begin(), blocks.end(), instruction,
      [](auto target, const auto& item) { return target < item.first_instruction; });
  if (block == blocks.begin()) return Error::kSite;
  --block;
  const auto allocation = locals_.allocations[environment->value];
  bool initialized = allocation && allocation->value >= block->first_instruction &&
      allocation->value < store->instruction.value && store->instruction.value >= block->first_instruction;
  const auto block_id = static_cast<std::size_t>(block - blocks.begin());
  if (!initialized && id < locals_.initializations.size() && locals_.initializations[id] &&
      block_id < locals_.block_entries.size()) {
    const auto& scope = *locals_.initializations[id];
    const auto entry = locals_.block_entries[block_id];
    initialized = scope.store.value < instruction && scope.begin <= entry && entry < scope.end;
  }
  if (!allocation || !initialized) return Error::kInitialization;
  if (writes_ == analysis::ClosureWrites::kUnknown) {
    const auto next = std::lower_bound(locals_.capture_barriers.begin(), locals_.capture_barriers.end(), creation);
    if (next != locals_.capture_barriers.begin() && (next - 1)->value >= allocation->value)
      return Error::kWrites;
  } else if (writes_ != analysis::ClosureWrites::kLanguage) return Error::kWrites;
  if (requirement == analysis::CaptureDomain::kDefined) {
    if (block_id >= defined_.block_entries.size()) return Error::kDomain;
    const auto entry = defined_.block_entries[block_id];
    auto found_guard = std::upper_bound(defined_.guards.begin(), defined_.guards.end(), std::tuple(id, entry),
        [](auto target, const auto& guard) { return target < std::tuple(guard.cell.value, guard.begin); });
    if (found_guard == defined_.guards.begin()) return Error::kDomain;
    const auto& guard = *--found_guard;
    if (guard.cell.value != id || guard.instruction.value >= instruction || entry < guard.begin ||
        entry >= guard.end) return Error::kDomain;
  }
  return CaptureCell{cell};
}
}  // namespace abcd::source
