#include "abcd/analysis/instruction_ssa.h"

#include <new>
#include <stdexcept>

namespace abcd::analysis {
std::variant<InstructionSsa, InstructionSsaError> InstructionSsa::Build(
    const ExceptionFlow& flow, const ExceptionSsa& ssa, std::size_t bytes) {
  using Error = InstructionSsaError;
  if (flow.instructions.empty() || flow.nodes.size() > UINT32_MAX)
    return Error::kInvalidFlow;
  if (flow.instructions.size() > bytes / sizeof(Sites)) return Error::kResourceLimit;
  try {
    InstructionSsa result{flow, ssa};
    result.sites_.resize(flow.instructions.size());
    if (result.AllocatedBytes() > bytes) return Error::kResourceLimit;
    for (std::size_t i = 0; i < flow.nodes.size(); ++i) {
      const auto& node = flow.nodes[i];
      if (node.instruction.value >= result.sites_.size()) return Error::kInvalidFlow;
      auto& site = result.sites_[node.instruction.value];
      const ExceptionNodeId id{static_cast<std::uint32_t>(i)};
      switch (node.phase) {
        case ExceptionPhase::kBefore:
          if (site.before) return Error::kInvalidFlow;
          site.before = id;
          break;
        case ExceptionPhase::kCommit:
          if (site.commit) return Error::kInvalidFlow;
          site.commit = id;
          break;
        case ExceptionPhase::kCatch:
          break;
      }
    }
    for (const auto& site : result.sites_)
      if (!site.before || !site.commit) return Error::kInvalidFlow;
    // Native reads are site-sorted. Index each group once instead of making
    // every AST/callee query binary-search the full method's read array.
    const auto reads = ssa.AllReads();
    for (std::size_t first = 0; first < reads.size();) {
      const auto node_id = reads[first].instruction.value;
      if (node_id >= flow.nodes.size()) return Error::kInvalidFlow;
      const auto& node = flow.nodes[node_id];
      if (node.phase != ExceptionPhase::kBefore) return Error::kInvalidFlow;
      auto end = first + 1;
      while (end < reads.size() && reads[end].instruction.value == node_id) ++end;
      if (end < reads.size() && reads[end].instruction.value < node_id)
        return Error::kInvalidFlow;
      result.sites_[node.instruction.value].reads = reads.subspan(first, end - first);
      first = end;
    }
    return result;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::span<const RegisterValue> InstructionSsa::Values() const {
  return std::visit([](auto ssa) { return ssa->Values(); }, ssa_);
}
std::size_t InstructionSsa::ValueCount() const { return Values().size(); }
std::size_t InstructionSsa::AllocatedBytes() const { return sites_.capacity() * sizeof(Sites); }
std::span<const SsaRead> InstructionSsa::Reads(InstructionId instruction) const {
  if (const auto* normal = std::get_if<const RegisterSsa*>(&ssa_))
    return (*normal)->Reads(instruction);
  if (instruction.value >= sites_.size() || !sites_[instruction.value].before) return {};
  return sites_[instruction.value].reads;
}
std::optional<SsaValueId> InstructionSsa::Write(InstructionId instruction) const {
  if (const auto* normal = std::get_if<const RegisterSsa*>(&ssa_))
    return (*normal)->Write(instruction);
  if (instruction.value >= sites_.size() || !sites_[instruction.value].commit) return {};
  return std::get<const ExceptionSsa*>(ssa_)->Write(*sites_[instruction.value].commit);
}
std::span<const PhiInput> InstructionSsa::Inputs(SsaValueId value) const {
  return std::visit([&](auto ssa) { return ssa->Inputs(value); }, ssa_);
}
std::optional<ScalarSlot> InstructionSsa::Slot(SsaValueId value) const {
  if (value.value >= Values().size()) return {};
  return Values()[value.value].slot;
}
bool InstructionSsa::IsPhi(SsaValueId value) const {
  return value.value < Values().size() &&
         std::holds_alternative<PhiValue>(Values()[value.value].origin);
}
std::optional<InstructionId> InstructionSsa::Definition(SsaValueId value) const {
  if (value.value >= Values().size()) return {};
  const auto* origin = std::get_if<InstructionValue>(&Values()[value.value].origin);
  if (!origin) return {};
  if (!flow_) return origin->instruction;
  if (origin->instruction.value >= flow_->nodes.size()) return {};
  const auto& node = flow_->nodes[origin->instruction.value];
  if (node.phase != ExceptionPhase::kCommit) return {};
  return node.instruction;
}
}  // namespace abcd::analysis
