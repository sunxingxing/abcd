#include "scope_facts.h"
#include "function_context.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <tuple>

namespace abcd::source {
std::uint64_t NormalScopeFacts::QueryWork() const {
  // Seven possible binary searches plus ancestry and bounded field checks.
  constexpr auto kChecks = std::numeric_limits<std::uint32_t>::digits;
  return 2 * kChecks + std::bit_width(locals_.cells.size()) +
      std::bit_width(locals_.stores.size()) + 2 * std::bit_width(method_.flow.blocks.size()) +
      std::bit_width(locals_.initializations.size()) +
      std::bit_width(locals_.capture_barriers.size()) + std::bit_width(defined_.guards.size());
}
bool NormalScopeFacts::Matches(analysis::InstructionId site,
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
bool NormalScopeFacts::Matches(analysis::InstructionId site, const ClassCaptureSite& creation) const {
  const auto* expected = CaptureClassCreation(creation);
  if (!expected || site.value >= method_.instructions.size()) return false;
  const auto& instruction = method_.instructions[site.value];
  const auto* operation = std::get_if<analysis::ClassCreation>(&instruction.operation);
  return operation && *operation == *expected && instruction.pc == CapturePc(creation) &&
      site.value < environments_.before.size() && environments_.before[site.value] &&
      *environments_.before[site.value] == creation.environment;
}
std::variant<CaptureCell, ScopeError> NormalScopeFacts::Rebase(
    analysis::InstructionId creation, analysis::CapturedSlot capture,
    analysis::CaptureDomain requirement, std::optional<analysis::InstructionId>* deferred) const {
  using Error = ScopeError;
  if (deferred) deferred->reset();
  switch (requirement) {
    case analysis::CaptureDomain::kLanguage:
    case analysis::CaptureDomain::kDefined:
    case analysis::CaptureDomain::kGuarded: break;
    default: return Error::kDomain;
  }
  // Guarded weakens only the initialization premise; every Language proof
  // below applies to it unchanged.
  const bool language = requirement != analysis::CaptureDomain::kDefined;
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
  const bool prior_store = store != locals_.stores.begin() && (--store)->cell.value == id;
  const auto& blocks = method_.flow.blocks;
  auto block = std::upper_bound(blocks.begin(), blocks.end(), instruction,
      [](auto target, const auto& item) { return target < item.first_instruction; });
  if (block == blocks.begin()) return Error::kSite;
  --block;
  const auto allocation = locals_.allocations[environment->value];
  bool initialized = prior_store && allocation && allocation->value >= block->first_instruction &&
      allocation->value < store->instruction.value && store->instruction.value >= block->first_instruction;
  const auto block_id = static_cast<std::size_t>(block - blocks.begin());
  if (!initialized) initialized = locals_.InitializationBefore(
      {id}, {static_cast<std::uint32_t>(block_id)}, creation).has_value();
  // A fresh cell may be captured before initialization when the original
  // allocation-to-store prefix proves no observer/escape. This discharges the
  // child's eventual Language reads, not a value at the creation site. Defined
  // requirements and unknown transitive writes retain the strict old rule.
  if (!initialized && language && writes_ == analysis::ClosureWrites::kLanguage &&
      id < locals_.unexposed_initializations.size() && locals_.unexposed_initializations[id]) {
    const auto& prefix = *locals_.unexposed_initializations[id];
    initialized = allocation && prefix.allocation == *allocation &&
        prefix.allocation.value >= block->first_instruction &&
        prefix.allocation.value < instruction && instruction < prefix.store.value;
  }
  if (!initialized && language && writes_ == analysis::ClosureWrites::kLanguage &&
      id < module_prefix_.size() && module_prefix_[id]) {
    const auto& prefix=*module_prefix_[id];
    initialized=allocation && prefix.allocation==*allocation &&
        (prefix.capture ? *prefix.capture==creation :
         prefix.allocation.value>=block->first_instruction &&
         prefix.allocation.value<instruction && instruction<prefix.store.value);
  }
  if (!initialized && requirement == analysis::CaptureDomain::kGuarded && deferred && allocation &&
      writes_ == analysis::ClosureWrites::kLanguage) {
    // The store must be the first write executed after this allocation. Its
    // block holds the allocation and creation, so either the cell's first
    // store follows the creation in that block (instruction order is execution
    // order there, other blocks run after it), or the cell's only store lies in
    // another block. Children observe the Hole only through their own guards
    // until then; the owner's native declaration at that store reproduces it,
    // and the owner tree independently requires that store to sit directly in
    // the allocation's own block statement.
    const auto first = std::lower_bound(locals_.stores.begin(), locals_.stores.end(), std::tuple(id, 0U),
        [](auto item, auto target) { return std::tuple(item.cell.value, item.instruction.value) < target; });
    const auto end = std::size_t(block->first_instruction) + block->instruction_count;
    if (first != locals_.stores.end() && first->cell.value == id &&
        allocation->value >= block->first_instruction && allocation->value < creation.value) {
      const bool same_block = first->instruction.value >= block->first_instruction && first->instruction.value < end;
      const auto next = std::next(first);
      const bool only = next == locals_.stores.end() || next->cell.value != id;
      if (same_block ? first->instruction.value > creation.value : only) {
        *deferred = first->instruction;
        initialized = true;
      }
    }
  }
  if (!allocation || !initialized) return Error::kInitialization;
  if (writes_ == analysis::ClosureWrites::kUnknown) {
    if (!locals_.PreservesSince(*allocation, creation)) return Error::kWrites;
  } else if (writes_ != analysis::ClosureWrites::kLanguage) return Error::kWrites;
  if (requirement == analysis::CaptureDomain::kDefined) {
    if (block_id >= defined_.block_entries.size()) return Error::kDomain;
    const auto entry = defined_.block_entries[block_id];
    auto found_guard = std::upper_bound(defined_.guards.begin(), defined_.guards.end(), std::tuple(id, entry),
        [](auto target, const auto& guard) { return target < std::tuple(guard.cell.value, guard.begin); });
    if (found_guard == defined_.guards.begin()) return Error::kDomain;
    const auto& guard = *--found_guard;
    if (guard.cell.value != id || guard.instruction.value >= method_.instructions.size() || entry < guard.begin ||
        entry >= guard.end) return Error::kDomain;
    const auto after_guard = std::upper_bound(blocks.begin(), blocks.end(), guard.instruction.value,
        [](auto target, const auto& item) { return target < item.first_instruction; });
    if (after_guard == blocks.begin()) return Error::kDomain;
    const auto guard_block = after_guard - 1;
    // A dominating guard in another block already completed on this path.
    // Instruction order is meaningful only inside its actual owning block.
    if (guard.instruction.value - guard_block->first_instruction >= guard_block->instruction_count ||
        (guard_block == block && guard.instruction >= creation)) return Error::kDomain;
  }
  return CaptureCell{cell};
}
std::uint64_t ScopeFacts::QueryWork() const {
  return std::visit([](const auto& input) { return input.QueryWork(); }, input_);
}
analysis::ClosureWrites ScopeFacts::Writes() const {
  return std::visit([](const auto& input) { return input.Writes(); }, input_);
}
std::optional<abc::CodeOffset> CapturePc(const CaptureSite& site) {
  return std::visit([](const auto& creation) -> std::optional<abc::CodeOffset> {
    if constexpr (std::is_same_v<std::decay_t<decltype(creation)>, analysis::CreationBinding>) return creation.offset;
    else {
      if (const auto* ctor = creation.owner.ConstructorSite()) return ctor->pc;
      if (const auto* member = creation.owner.ClassSite()) return member->pc;
      return std::nullopt;
    }
  }, site);
}
abc::MethodPosition CapturePosition(const CaptureSite& site) {
  return std::visit([](const auto& creation) {
    if constexpr (std::is_same_v<std::decay_t<decltype(creation)>, analysis::CreationBinding>) return creation.target.position;
    else return creation.owner.Position();
  }, site);
}
const analysis::ClassCreation* CaptureClassCreation(const ClassCaptureSite& site) {
  if (const auto* constructor = site.owner.ConstructorSite()) return &constructor->creation;
  if (const auto* member = site.owner.ClassSite()) return &member->creation;
  return nullptr;
}
bool ScopeFacts::Matches(analysis::InstructionId site, const CaptureSite& creation) const {
  return std::visit([&](const auto& input, const auto& occurrence) {
    return input.Matches(site, occurrence);
  }, input_, creation);
}
std::variant<CaptureCell, ScopeError> ScopeFacts::Rebase(analysis::InstructionId site,
    analysis::CapturedSlot capture, analysis::CaptureDomain domain,
    std::optional<analysis::InstructionId>* deferred) const {
  if (const auto* normal = std::get_if<NormalScopeFacts>(&input_))
    return normal->Rebase(site, capture, domain, deferred);
  // Exception owners keep the strict initialization proof for every domain.
  if (deferred) deferred->reset();
  return std::get<ExceptionScopeFacts>(input_).Rebase(site, capture, domain);
}
}  // namespace abcd::source
