#ifndef ABCD_ANALYSIS_REGISTER_SSA_BUILDER_H_
#define ABCD_ANALYSIS_REGISTER_SSA_BUILDER_H_

#include "abcd/analysis/register_ssa.h"
#include "idf_scratch.h"

namespace abcd::analysis {
struct RegisterSsa::Builder {
  struct PhiSite {
    BlockId block;
    RegisterBindingId binding;
    SsaValueId value;
  };
  struct Retained {
    detail::IdfScratch& memory;
    std::size_t bytes;
    Retained(detail::IdfScratch& resource, std::size_t count)
        : memory(resource), bytes(count) {
      memory.Retain(bytes);
    }
    ~Retained() { memory.Release(bytes); }
    Retained(const Retained&) = delete;
    Retained& operator=(const Retained&) = delete;
  };
  Builder(const abc::MethodBody& input, const NormalFlow& flow,
          std::uint64_t frame, IdfQuota& work, IdfBudget budget,
          std::span<const SemanticInstruction> lifted)
      : body(input),
        graph(flow),
        semantics(lifted),
        frame_size(frame),
        quota(work),
        initial_units(work.Consumed()),
        memory(budget.bytes) {}
  void Charge(std::uint64_t units = 1) {
    if (!quota.Take(units)) throw detail::IdfWorkLimit{};
  }
  std::optional<IdfError> Place(const RegisterUses& uses,
                                const IdfIndex& index);
  void Rename(const RegisterUses& uses, const DominatorTree& tree);
  SsaValueId Define(
      ScalarSlot slot,
      std::variant<MethodEntry, InstructionValue, PhiValue> origin) {
    Charge();
    if (values.size() >= UINT32_MAX) throw std::bad_alloc{};
    const SsaValueId id{static_cast<std::uint32_t>(values.size())};
    values.push_back({slot, origin});
    return id;
  }
  RegisterSsa Publish();
  const abc::MethodBody& body;
  const NormalFlow& graph;
  std::span<const SemanticInstruction> semantics;
  std::uint64_t frame_size;
  IdfQuota& quota;
  std::uint64_t initial_units;
  std::uint64_t instructions = 0;
  detail::IdfScratch memory;
  std::pmr::vector<PhiSite> phis{&memory};
  std::pmr::vector<RegisterValue> values{&memory};
  std::pmr::vector<SsaRead> reads{&memory};
  std::pmr::vector<SsaWrite> writes{&memory};
  std::pmr::vector<PhiInput> inputs{&memory};
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_REGISTER_SSA_BUILDER_H_
