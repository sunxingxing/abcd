#ifndef ABCD_SOURCE_DEFAULT_BODY_H_
#define ABCD_SOURCE_DEFAULT_BODY_H_

#include "default_value_proof.h"
#include "cell_default_proof.h"
#include "cell_parameter_default.h"
#include <memory>
#include "expression_constants.h"

namespace abcd::source {
enum class DefaultBodyError : std::uint8_t {
  kRevision, kShape, kProof, kFlow, kOrigin, kResourceLimit, kWorkLimit
};
struct InitializedParameterCommit {
  std::uint32_t parameter;
  analysis::ValueId initialized_entry;
  analysis::OpId store;
  ParameterCellIdentity destination;
};
struct SignatureCapture {
  std::uint32_t parameter;
  analysis::OpId read, guard;
  analysis::InstructionId site;
  analysis::LexicalAddress address;
  IncomingCell storage;
};
struct DefaultInitializer {
  std::uint32_t parameter;
  analysis::ValueOperand fallback;
  analysis::OpId initializer;
  std::optional<abc::PoolIndex> text;
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
  [[nodiscard]] static std::variant<DefaultBody, DefaultBodyError> BuildCellParameter(
      const analysis::ValueProgram&, const analysis::ValueEnvironments&, const CellPlan&,
      const ParameterPlan&, std::uint32_t length, analysis::SemanticBudget budget = {});
  const CellParameterDefaultProof* CellParameters() const { return std::get_if<CellParameterDefaultProof>(&proof_); }
  std::span<const InitializedParameterCommit> ParameterCommits() const { return commits_; }
  std::span<const SignatureCapture> SignatureCaptures() const { return captures_; }
  std::span<const analysis::ValueExceptionalExit> SignatureExceptions() const { return exceptions_; }
  const SignatureCapture* CaptureAt(analysis::InstructionId) const;
  const SignatureCapture* CaptureParameter(std::uint32_t) const;
  std::variant<CellParameterDefaultCost, CellParameterDefaultFailure> VerifyCellParameterSource(analysis::SemanticBudget) const;
  [[nodiscard]] const analysis::ValueProgram& Graph() const { return graph_; }
  [[nodiscard]] DefaultSignatureView Signature() const {
    if (CellParameters()) return {initializers_, joint_operations_};
    return {initializers_, std::visit([](const auto& proof) -> std::span<const DefaultOpPlacement> {
      if constexpr (std::is_same_v<std::decay_t<decltype(proof)>, CellParameterDefaultProof>) return {};
      else return proof.Operations();
    }, proof_)};
  }
  [[nodiscard]] std::span<const SignatureString> Strings() const { return strings_; }
  [[nodiscard]] DefaultStorage Storage() const {
    return (std::holds_alternative<CellDefaultProof>(proof_) || CellParameters()) ? DefaultStorage::kCell : DefaultStorage::kValue;
  }
  [[nodiscard]] std::size_t AllocatedBytes() const { return allocated_bytes_; }
  [[nodiscard]] std::uint64_t Work() const { return work_; }
 private:
  DefaultBody(analysis::ValueProgram graph, std::variant<DefaultValueProof, CellDefaultProof, CellParameterDefaultProof> proof,
              std::vector<DefaultInitializer> initializers, std::vector<SignatureString> strings,
              std::size_t bytes, std::uint64_t work)
      : graph_(std::move(graph)), proof_(std::move(proof)), initializers_(std::move(initializers)), strings_(std::move(strings)), allocated_bytes_(bytes), work_(work) {}
  analysis::ValueProgram graph_;
  std::variant<DefaultValueProof, CellDefaultProof, CellParameterDefaultProof> proof_;
  std::vector<DefaultInitializer> initializers_;
  std::vector<SignatureString> strings_;
  // Original input remains immutable for independent signature verification.
  std::unique_ptr<analysis::ValueProgram> original_;
  std::vector<DefaultOpPlacement> joint_operations_;
  std::vector<SignatureCapture> captures_;
  std::vector<InitializedParameterCommit> commits_;
  std::vector<std::uint32_t> capture_sites_;
  std::vector<analysis::ValueExceptionalExit> exceptions_;
  std::uint32_t formal_count_ = 0, length_ = 0;
  std::size_t allocated_bytes_;
  std::uint64_t work_;
};
}  // namespace abcd::source
#endif
