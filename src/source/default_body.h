#ifndef ABCD_SOURCE_DEFAULT_BODY_H_
#define ABCD_SOURCE_DEFAULT_BODY_H_

#include "default_value_proof.h"

namespace abcd::source {
enum class DefaultBodyError : std::uint8_t {
  kRevision, kShape, kProof, kFlow, kOrigin, kResourceLimit, kWorkLimit
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
  [[nodiscard]] const analysis::ValueProgram& Graph() const { return graph_; }
  [[nodiscard]] const DefaultValueProof& Signature() const { return proof_; }
  [[nodiscard]] std::size_t AllocatedBytes() const { return allocated_bytes_; }
  [[nodiscard]] std::uint64_t Work() const { return work_; }
 private:
  DefaultBody(analysis::ValueProgram graph, DefaultValueProof proof,
              std::size_t bytes, std::uint64_t work)
      : graph_(std::move(graph)), proof_(std::move(proof)), allocated_bytes_(bytes), work_(work) {}
  analysis::ValueProgram graph_;
  DefaultValueProof proof_;
  std::size_t allocated_bytes_;
  std::uint64_t work_;
};
}  // namespace abcd::source
#endif
