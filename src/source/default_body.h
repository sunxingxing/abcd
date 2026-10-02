#ifndef ABCD_SOURCE_DEFAULT_BODY_H_
#define ABCD_SOURCE_DEFAULT_BODY_H_

#include "default_value_proof.h"
#include "cell_default_proof.h"

namespace abcd::source {
enum class DefaultBodyError : std::uint8_t {
  kRevision, kShape, kProof, kFlow, kOrigin, kResourceLimit, kWorkLimit
};
struct DefaultInitializer {
  std::uint32_t parameter;
  analysis::ValueOperand fallback;
  analysis::OpId initializer;
};
enum class DefaultStorage : std::uint8_t { kValue, kCell };
struct DefaultSignatureView {
  std::span<const DefaultInitializer> defaults;
  std::span<const DefaultOpPlacement> operations;
  auto Defaults() const { return defaults; }
  auto Operations() const { return operations; }
};
// Residual body under INITIALIZED source-formal inputs, not an equivalent
// standalone ABC method. Its private owner pairs the graph with the prologue
// proof. The source factory must add/prove the native signature before any
// publication; no input-correspondence certificate is copied into this graph.
class DefaultBody {
 public:
  DefaultBody(DefaultBody&&) noexcept = default;
  DefaultBody& operator=(DefaultBody&&) noexcept = default;
  [[nodiscard]] static std::variant<DefaultBody, DefaultBodyError> Build(
      const analysis::ValueProgram& original, const ParameterPlan& parameters,
      const DefaultCandidates& candidates, std::uint32_t length,
      analysis::SemanticBudget budget = {});
  [[nodiscard]] static std::variant<DefaultBody, DefaultBodyError> BuildCaptured(
      const analysis::ValueProgram&, const analysis::ValueEnvironments&,
      const CellPlan&, const ParameterPlan&, const DefaultCandidates&,
      std::uint32_t length, analysis::SemanticBudget budget = {});
  [[nodiscard]] const CapturedDefaultBinding* Capture() const {
    const auto* proof = std::get_if<DefaultValueProof>(&proof_);
    return proof && proof->Capture() ? &*proof->Capture() : nullptr;
  }
  [[nodiscard]] static std::variant<DefaultBody, DefaultBodyError> BuildCell(
      const analysis::ValueProgram& original, const analysis::ValueEnvironments& environments,
      const CellPlan& cells, const ParameterPlan& parameters, const CellDefaults& candidates,
      std::uint32_t length, analysis::SemanticBudget budget = {});
  [[nodiscard]] const analysis::ValueProgram& Graph() const { return graph_; }
  [[nodiscard]] DefaultSignatureView Signature() const {
    return {initializers_, std::visit([](const auto& proof) { return proof.Operations(); }, proof_)};
  }
  [[nodiscard]] DefaultStorage Storage() const {
    return std::holds_alternative<CellDefaultProof>(proof_) ? DefaultStorage::kCell : DefaultStorage::kValue;
  }
  [[nodiscard]] std::size_t AllocatedBytes() const { return allocated_bytes_; }
  [[nodiscard]] std::uint64_t Work() const { return work_; }
 private:
  DefaultBody(analysis::ValueProgram graph, std::variant<DefaultValueProof, CellDefaultProof> proof,
              std::vector<DefaultInitializer> initializers,
              std::size_t bytes, std::uint64_t work)
      : graph_(std::move(graph)), proof_(std::move(proof)), initializers_(std::move(initializers)), allocated_bytes_(bytes), work_(work) {}
  analysis::ValueProgram graph_;
  std::variant<DefaultValueProof, CellDefaultProof> proof_;
  std::vector<DefaultInitializer> initializers_;
  std::size_t allocated_bytes_;
  std::uint64_t work_;
};
}  // namespace abcd::source
#endif
