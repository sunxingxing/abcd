#include "abcd/analysis/register_ssa.h"

#include <algorithm>
#include <stdexcept>

#include "register_ssa_builder.h"

namespace abcd::analysis {
std::span<const SsaRead> RegisterSsa::Reads(InstructionId instruction) const {
  if (!reads_) return {};
  const std::span<const SsaRead> all{reads_.get(), read_count_};
  const auto first =
      std::lower_bound(all.begin(), all.end(), instruction,
                       [](const SsaRead& read, InstructionId id) {
                         return read.instruction < id;
                       });
  const auto last = std::upper_bound(first, all.end(), instruction,
                                     [](InstructionId id, const SsaRead& read) {
                                       return id < read.instruction;
                                     });
  return {first, last};
}
std::optional<SsaValueId> RegisterSsa::Write(InstructionId instruction) const {
  if (!writes_) return {};
  const std::span<const SsaWrite> all{writes_.get(), write_count_};
  const auto found =
      std::lower_bound(all.begin(), all.end(), instruction,
                       [](const SsaWrite& write, InstructionId id) {
                         return write.instruction < id;
                       });
  if (found == all.end() || found->instruction != instruction) return {};
  return found->value;
}
std::span<const PhiInput> RegisterSsa::Inputs(SsaValueId phi) const {
  if (!inputs_) return {};
  const std::span<const PhiInput> all{inputs_.get(), input_count_};
  const auto first = std::lower_bound(
      all.begin(), all.end(), phi,
      [](const PhiInput& input, SsaValueId id) { return input.phi < id; });
  const auto last = std::upper_bound(
      first, all.end(), phi,
      [](SsaValueId id, const PhiInput& input) { return id < input.phi; });
  return {first, last};
}
std::size_t RegisterSsa::AllocatedBytes() const {
  return Values().size_bytes() + (reads_ ? read_count_ * sizeof(SsaRead) : 0) +
         (writes_ ? write_count_ * sizeof(SsaWrite) : 0) +
         (inputs_ ? input_count_ * sizeof(PhiInput) : 0);
}

std::optional<IdfError> RegisterSsa::Builder::Place(const RegisterUses& uses,
                                                    const IdfIndex& index) {
  for (std::size_t binding = 0; binding < uses.Bindings().size(); ++binding) {
    Charge();
    const RegisterBindingId id{binding};
    const auto definitions = uses.Definitions(id), upward = uses.UpwardUses(id);
    if (definitions.empty() || upward.empty()) continue;
    auto live_result =
        index.LiveIn(definitions, upward, quota, {memory.Available()});
    if (const auto* error = std::get_if<IdfError>(&live_result)) return *error;
    const auto& live = std::get<BindingLiveness>(live_result);
    memory.Observe(live.work.peak_bytes);
    Retained live_bytes{memory, live.AllocatedBytes()};
    auto placed_result =
        index.Place(definitions, quota, live.Blocks(), {memory.Available()});
    if (const auto* error = std::get_if<IdfError>(&placed_result))
      return *error;
    const auto& placed = std::get<PhiPlacement>(placed_result);
    memory.Observe(placed.work.peak_bytes);
    Retained placed_bytes{memory, placed.AllocatedBytes()};
    for (const auto block : placed.Blocks()) {
      Charge();
      phis.push_back({block, id, {}});
    }
  }
  std::sort(phis.begin(), phis.end(), [&](const PhiSite& a, const PhiSite& b) {
    Charge();
    return a.block != b.block ? a.block < b.block
                              : a.binding.value < b.binding.value;
  });
  return {};
}

RegisterSsa RegisterSsa::Builder::Publish() {
  RegisterSsa result;
  const auto copy = [&]<class T>(const std::pmr::vector<T>& source,
                                 std::unique_ptr<T[]>& target) {
    Charge(source.size());
    if (source.size() > memory.Available() / sizeof(T)) throw std::bad_alloc{};
    if (!source.empty()) {
      target = std::make_unique<T[]>(source.size());
      memory.Retain(source.size() * sizeof(T));
      std::copy(source.begin(), source.end(), target.get());
    }
  };
  // Reserve copies together with all scratch/external owners still alive.
  // These final arrays own their allocator independently of Builder's PMR.
  copy(values, result.values_);
  copy(reads, result.reads_);
  copy(writes, result.writes_);
  copy(inputs, result.inputs_);
  result.value_count_ = values.size();
  result.read_count_ = reads.size();
  result.write_count_ = writes.size();
  result.input_count_ = inputs.size();
  result.work = {instructions, quota.Consumed() - initial_units, phis.size(),
                 memory.Peak()};
  return result;
}

std::variant<RegisterSsa, RegisterUseFailure, IdfError, DominatorError,
             SsaError>
RegisterSsa::Build(const abc::MethodBody& body, const NormalFlow& graph,
                   std::uint64_t frame_size, IdfQuota& quota,
                   IdfBudget budget,
                   std::span<const SemanticInstruction> semantics) {
  return BuildImpl(&body, graph, frame_size, quota, budget, semantics, {});
}
std::variant<RegisterSsa, RegisterUseFailure, IdfError, DominatorError, SsaError>
RegisterSsa::BuildSites(std::span<const RegisterSite> sites, const NormalFlow& graph,
                       std::uint64_t frame_size, IdfQuota& quota, IdfBudget budget) {
  return BuildImpl(nullptr, graph, frame_size, quota, budget, {}, sites);
}
std::variant<RegisterSsa, RegisterUseFailure, IdfError, DominatorError, SsaError>
RegisterSsa::BuildImpl(const abc::MethodBody* body, const NormalFlow& graph,
    std::uint64_t frame_size, IdfQuota& quota, IdfBudget budget,
    std::span<const SemanticInstruction> semantics,
    std::span<const RegisterSite> sites) {
  try {
    Builder builder{body, graph, frame_size, quota, budget, semantics, sites};
    auto used = body
        ? RegisterUses::Build(*body, graph, frame_size, quota,
                             {builder.memory.Available()}, semantics)
        : RegisterUses::BuildSites(sites, graph, frame_size, quota,
                                  {builder.memory.Available()});
    if (const auto* error = std::get_if<RegisterUseFailure>(&used))
      return *error;
    const auto& uses = std::get<RegisterUses>(used);
    builder.memory.Observe(uses.work.peak_bytes);
    Builder::Retained uses_bytes{builder.memory, uses.AllocatedBytes()};
    {
      builder.Charge(graph.blocks.size());
      auto indexed = IdfIndex::Build(graph, {}, {builder.memory.Available()});
      if (const auto* error = std::get_if<IdfError>(&indexed)) return *error;
      const auto& index = std::get<IdfIndex>(indexed);
      builder.memory.Observe(index.PeakBytes());
      Builder::Retained index_bytes{builder.memory, index.AllocatedBytes()};
      if (const auto error = builder.Place(uses, index)) return *error;
    }
    builder.Charge(graph.blocks.size());
    auto dominated =
        BuildNormalDominators(graph, {}, {builder.memory.Available()});
    if (const auto* error = std::get_if<DominatorError>(&dominated))
      return *error;
    const auto& tree = std::get<DominatorTree>(dominated);
    builder.memory.Observe(tree.work.allocated_bytes);
    Builder::Retained tree_bytes{builder.memory,
                                 tree.nodes.capacity() * sizeof(DominatorNode)};
    builder.Rename(uses, tree);
    return builder.Publish();
  } catch (const detail::IdfWorkLimit&) {
    return SsaError::kWorkLimit;
  } catch (const std::bad_alloc&) {
    return SsaError::kResourceLimit;
  } catch (const std::length_error&) {
    return SsaError::kResourceLimit;
  }
}
}  // namespace abcd::analysis
