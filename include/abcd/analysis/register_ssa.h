#ifndef ABCD_ANALYSIS_REGISTER_SSA_H_
#define ABCD_ANALYSIS_REGISTER_SSA_H_

#include "abcd/analysis/dominators.h"
#include "abcd/analysis/register_uses.h"

namespace abcd::analysis {
struct InstructionId {
  std::uint32_t value = 0;
  auto operator<=>(const InstructionId&) const = default;
};
struct SsaValueId {
  std::uint32_t value = 0;
  auto operator<=>(const SsaValueId&) const = default;
};
struct MethodEntry {};
struct InstructionValue {
  InstructionId instruction;
};
struct PhiValue {
  BlockId block;
};
struct RegisterValue {
  ScalarSlot slot;
  std::variant<MethodEntry, InstructionValue, PhiValue> origin;
};
struct SsaRead {
  InstructionId instruction;
  // Ordinal in RegisterEffect reads followed by expanded range, not ABC
  // operand. Fixed inputs plus a range can exceed a 16-bit encoded count;
  // keep this width aligned with value-IR and guarded-read occurrences.
  std::uint32_t ordinal = 0;
  SsaValueId value;
};
struct SsaWrite {
  InstructionId instruction;
  SsaValueId value;
};
struct SsaEdge {
  BlockId source;
  std::uint8_t successor = 0;
};
struct PhiInput {
  SsaValueId phi;
  std::variant<MethodEntry, SsaEdge> edge;
  SsaValueId value;
};
enum class SsaError { kWorkLimit, kResourceLimit };
// Complete IDF keeps state joins even without upward reads. Owners can query
// persistent state snapshots without inserting a dense binding/read matrix.
enum class SsaPlacement { kPruned, kComplete };
struct SsaWork {
  std::uint64_t rename_instructions = 0;
  std::uint64_t units = 0;
  std::size_t phis = 0;
  std::size_t peak_bytes = 0;
};

// Register SSA only: entry values are UNKNOWN VM values, not JS undefined or
// proof of language-value/sentinel type. Instructions retain all their effects.
// Body API requires validated body and unchanged normal CFG; rejects handlers/
// resumes. BuildSites instead accepts an owner-certified effect program and
// complete graph, including an explicitly staged exceptional topology.
// Owned immutable arrays, no borrowed body/graph/index or PMR pointers. Build
// is a bounded synchronous primitive, not the lazy function-task service.
// A nonempty semantic span supplies the owner-proven effects for every body
// instruction, including unreachable ones. PCs and register bounds are checked;
// semantic equivalence is the owner's obligation. The span is borrowed only
// during Build and must remain immutable, as must body and graph.
class RegisterSsa {
 public:
  RegisterSsa(RegisterSsa&&) noexcept = default;
  RegisterSsa& operator=(RegisterSsa&&) noexcept = default;
  [[nodiscard]] static std::variant<RegisterSsa, RegisterUseFailure, IdfError,
                                    DominatorError, SsaError>
  Build(const abc::MethodBody& body, const NormalFlow& graph,
        std::uint64_t frame_size, IdfQuota& quota, IdfBudget budget = {},
        std::span<const SemanticInstruction> semantics = {});
  // SSA IDs index this supplied effect program. In particular InstructionValue
  // is a site index, not an original ABC instruction index; owners retain the
  // provenance mapping. No fabricated opcode/body or exception bypass is used.
  [[nodiscard]] static std::variant<RegisterSsa, RegisterUseFailure, IdfError,
                                    DominatorError, SsaError>
  BuildSites(std::span<const RegisterSite> sites, const NormalFlow& graph,
             std::uint64_t frame_size, IdfQuota& quota, IdfBudget budget = {},
             SsaPlacement placement = SsaPlacement::kPruned);
  [[nodiscard]] std::span<const RegisterValue> Values() const {
    return {values_.get(), values_ ? value_count_ : 0};
  }
  [[nodiscard]] std::span<const SsaRead> Reads(InstructionId instruction) const;
  // Sorted by instruction then operand ordinal; borrowed from this immutable
  // SSA. Sequential consumers should not repeat per-instruction binary lookup.
  [[nodiscard]] std::span<const SsaRead> AllReads() const {
    return {reads_.get(), reads_ ? read_count_ : 0};
  }
  [[nodiscard]] std::span<const PhiInput> AllInputs() const {
    return {inputs_.get(), inputs_ ? input_count_ : 0};
  }
  [[nodiscard]] std::optional<SsaValueId> Write(
      InstructionId instruction) const;
  [[nodiscard]] std::span<const PhiInput> Inputs(SsaValueId phi) const;
  [[nodiscard]] std::size_t AllocatedBytes() const;
  SsaWork work;

 private:
  static std::variant<RegisterSsa, RegisterUseFailure, IdfError,
                      DominatorError, SsaError>
  BuildImpl(const abc::MethodBody* body, const NormalFlow& graph,
            std::uint64_t frame_size, IdfQuota& quota, IdfBudget budget,
            std::span<const SemanticInstruction> semantics,
            std::span<const RegisterSite> sites,
            SsaPlacement placement = SsaPlacement::kPruned);
  struct Builder;
  RegisterSsa() = default;
  std::unique_ptr<RegisterValue[]> values_;
  std::unique_ptr<SsaRead[]> reads_;
  std::unique_ptr<SsaWrite[]> writes_;
  std::unique_ptr<PhiInput[]> inputs_;
  std::size_t value_count_ = 0;
  std::size_t read_count_ = 0;
  std::size_t write_count_ = 0;
  std::size_t input_count_ = 0;
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_REGISTER_SSA_H_
