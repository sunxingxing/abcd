#include "abcd/analysis/register_uses.h"

#include <algorithm>
#include <memory_resource>
#include <stdexcept>

#include "idf_scratch.h"

namespace abcd::analysis {
namespace {
bool SlotLess(ScalarSlot left, ScalarSlot right) {
  const auto* a = std::get_if<abc::RegisterId>(&left);
  const auto* b = std::get_if<abc::RegisterId>(&right);
  if (!a || !b) return !a && b;
  return a->value < b->value;
}
bool SameSlot(ScalarSlot left, ScalarSlot right) {
  return !SlotLess(left, right) && !SlotLess(right, left);
}
enum class AccessKind { kRead, kWrite };
struct RegisterEvent {
  ScalarSlot slot;
  BlockId block;
  std::uint32_t instruction;
  AccessKind kind;
};
}  // namespace

std::optional<RegisterBindingId> RegisterUses::Find(ScalarSlot slot) const {
  const auto bindings = Bindings();
  const auto found =
      std::lower_bound(bindings.begin(), bindings.end(), slot,
                       [](const RegisterBinding& item, ScalarSlot key) {
                         return SlotLess(item.slot, key);
                       });
  if (found == bindings.end() || !SameSlot(found->slot, slot)) return {};
  return RegisterBindingId{static_cast<std::size_t>(found - bindings.begin())};
}
std::span<const BlockId> RegisterUses::Definitions(RegisterBindingId id) const {
  if (id.value >= Bindings().size()) return {};
  const auto& binding = bindings_[id.value];
  if (binding.definitions_count == 0) return {};
  return {definitions_.get() + binding.definitions_begin,
          binding.definitions_count};
}
std::span<const BlockId> RegisterUses::UpwardUses(RegisterBindingId id) const {
  if (id.value >= Bindings().size()) return {};
  const auto& binding = bindings_[id.value];
  if (binding.upward_count == 0) return {};
  return {upward_.get() + binding.upward_begin, binding.upward_count};
}
std::size_t RegisterUses::AllocatedBytes() const {
  return Bindings().size_bytes() +
         (definitions_ ? definitions_count_ * sizeof(BlockId) : 0) +
         (upward_ ? upward_count_ * sizeof(BlockId) : 0);
}

std::variant<RegisterUses, RegisterUseFailure> RegisterUses::Build(
    const abc::MethodBody& body, const NormalFlow& graph,
    std::uint64_t frame_size, IdfQuota& quota, IdfBudget budget) {
  using Error = RegisterUseError;
  if (!body.tries.empty() || !body.handlers.empty())
    return RegisterUseFailure{Error::kExceptionRegions, {}, {}};
  if (body.instructions.size() > UINT32_MAX)
    return RegisterUseFailure{Error::kResourceLimit, {}, {}};
  try {
    const auto consumed = quota.Consumed();
    const auto charge = [&](std::uint64_t units = 1) {
      if (!quota.Take(units)) throw detail::IdfWorkLimit{};
    };
    std::size_t covered = 0;
    for (const auto& block : graph.blocks) {
      charge();
      if (block.first_instruction != covered || block.instruction_count == 0 ||
          block.instruction_count > body.instructions.size() - covered ||
          block.successor_count > block.successors.size())
        return RegisterUseFailure{Error::kInvalidGraph, block.begin, {}};
      covered += block.instruction_count;
      for (std::size_t i = 0; i < block.successor_count; ++i) {
        charge();
        const auto edge = block.successors[i];
        if (edge.target.value >= graph.blocks.size())
          return RegisterUseFailure{Error::kInvalidGraph, block.begin, {}};
        switch (edge.kind) {
          case FlowEdgeKind::kTaken:
          case FlowEdgeKind::kFallthrough:
            break;
          case FlowEdgeKind::kResume:
            return RegisterUseFailure{Error::kResumeEdge, block.begin, {}};
          default:
            return RegisterUseFailure{Error::kInvalidGraph, block.begin, {}};
        }
      }
    }
    if (graph.blocks.empty() || covered != body.instructions.size())
      return RegisterUseFailure{Error::kInvalidGraph, {}, {}};
    detail::IdfScratch memory{budget.bytes};
    charge(graph.blocks.size());
    std::pmr::vector<std::uint8_t> reached(graph.blocks.size(), 0, &memory);
    std::pmr::vector<BlockId> pending{&memory};
    std::pmr::vector<RegisterEvent> events{&memory};
    reached[0] = 1;
    pending.push_back({0});
    RegisterUses result;
    for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) {
      charge();
      const auto id = pending[cursor];
      const auto& block = graph.blocks[id.value];
      for (std::size_t pc = block.first_instruction;
           pc < std::size_t(block.first_instruction) + block.instruction_count;
           ++pc) {
        charge();
        ++result.work.instructions;
        const auto& instruction = body.instructions[pc];
        const auto lifted = LiftRegisterEffect(instruction.decoded, frame_size);
        if (const auto* error = std::get_if<ScalarError>(&lifted))
          return RegisterUseFailure{Error::kInstruction, instruction.offset,
                                    *error};
        const auto& effect = std::get<RegisterEffect>(lifted);
        const auto append = [&](ScalarSlot slot, AccessKind kind) {
          charge();
          events.push_back({slot, id, static_cast<std::uint32_t>(pc), kind});
        };
        for (std::size_t i = 0; i < effect.read_count; ++i)
          append(effect.reads[i], AccessKind::kRead);
        if (effect.range) {
          for (std::uint32_t i = 0; i < effect.range->count; ++i)
            append(abc::RegisterId{effect.range->first.value + i},
                   AccessKind::kRead);
        }
        if (effect.write) append(*effect.write, AccessKind::kWrite);
      }
      for (std::size_t i = 0; i < block.successor_count; ++i) {
        charge();
        const auto target = block.successors[i].target;
        if (!reached[target.value]) {
          reached[target.value] = 1;
          pending.push_back(target);
        }
      }
    }
    result.work.accesses = events.size();
    std::sort(events.begin(), events.end(), [&](const auto& a, const auto& b) {
      charge();
      if (!SameSlot(a.slot, b.slot)) return SlotLess(a.slot, b.slot);
      if (a.block != b.block) return a.block < b.block;
      if (a.instruction != b.instruction) return a.instruction < b.instruction;
      return a.kind < b.kind;
    });
    std::pmr::vector<RegisterBinding> bindings{&memory};
    std::pmr::vector<BlockId> definitions{&memory}, upward{&memory};
    // One sorted pass, not one CFG scan per binding. A self-copy reads before
    // writing; later reads after that first definition cannot seed live-in.
    for (std::size_t cursor = 0; cursor < events.size();) {
      RegisterBinding binding{events[cursor].slot, definitions.size(), 0,
                              upward.size(), 0};
      do {
        const auto block = events[cursor].block;
        bool defined = false, used = false;
        do {
          charge();
          if (events[cursor].kind == AccessKind::kWrite)
            defined = true;
          else if (!defined)
            used = true;
          ++cursor;
        } while (cursor < events.size() &&
                 SameSlot(events[cursor].slot, binding.slot) &&
                 events[cursor].block == block);
        if (defined) definitions.push_back(block);
        if (used) upward.push_back(block);
      } while (cursor < events.size() &&
               SameSlot(events[cursor].slot, binding.slot));
      binding.definitions_count =
          definitions.size() - binding.definitions_begin;
      binding.upward_count = upward.size() - binding.upward_begin;
      bindings.push_back(binding);
    }
    // Admit owned arrays while all scratch is still live, including vector
    // growth overlap. No scratch pointer escapes and failure publishes nothing.
    std::size_t output_bytes = 0;
    const auto copy = [&]<class T>(const std::pmr::vector<T>& source,
                                   std::unique_ptr<T[]>& target) {
      charge(source.size());
      if (source.size() >
          (budget.bytes - memory.Live() - output_bytes) / sizeof(T))
        throw std::bad_alloc{};
      if (!source.empty()) {
        target = std::make_unique<T[]>(source.size());
        output_bytes += source.size() * sizeof(T);
        std::copy(source.begin(), source.end(), target.get());
      }
    };
    copy(bindings, result.bindings_);
    copy(definitions, result.definitions_);
    copy(upward, result.upward_);
    result.count_ = bindings.size();
    result.definitions_count_ = definitions.size();
    result.upward_count_ = upward.size();
    result.work.units = quota.Consumed() - consumed;
    result.work.peak_bytes =
        std::max(memory.Peak(), memory.Live() + output_bytes);
    return result;
  } catch (const detail::IdfWorkLimit&) {
    return RegisterUseFailure{Error::kWorkLimit, {}, {}};
  } catch (const std::bad_alloc&) {
    return RegisterUseFailure{Error::kResourceLimit, {}, {}};
  } catch (const std::length_error&) {
    return RegisterUseFailure{Error::kResourceLimit, {}, {}};
  }
}
}  // namespace abcd::analysis
