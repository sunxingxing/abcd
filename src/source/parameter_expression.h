#ifndef ABCD_SOURCE_PARAMETER_EXPRESSION_H_
#define ABCD_SOURCE_PARAMETER_EXPRESSION_H_
#include <memory>

#include "default_value_proof.h"
namespace abcd::source {
enum class ParameterExpressionError : std::uint8_t {
  kRevision,
  kParameter,
  kControl,
  kExpression,
  kOrder,
  kEscape,
  kScope,
  kCatchState,
  kCorrespondence,
  kResourceLimit,
  kWorkLimit
};
struct ParameterExpressionFailure {
  ParameterExpressionError kind;
  std::optional<std::uint32_t> parameter;
  std::optional<analysis::OpId> operation;
};
struct ParameterExpressionCertificate {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::uint32_t formals = 0, length = 0;
  std::vector<DefaultCandidate> defaults;
  std::vector<DefaultOpPlacement> events;
  std::vector<analysis::ValueExceptionalExit> exceptions;
};
struct ParameterExpressionInput {
  std::shared_ptr<const analysis::ValueProgram> graph;
  ParameterExpressionCertificate certificate;
};
enum class ParameterRegionRole : std::uint8_t { kTest, kInitializer };
struct ParameterRegionBlock {
  analysis::WorkBlockId block;
  std::uint32_t parameter;
  ParameterRegionRole role;
};
struct ParameterRegions {
  std::vector<ParameterRegionBlock> blocks;
  std::size_t allocated_bytes;
  std::uint64_t work;
};
std::variant<ParameterRegions, ParameterExpressionFailure>
DeriveParameterRegions(const analysis::ValueProgram&,
                       std::span<const DefaultCandidate>,
                       analysis::SemanticBudget = {});
struct ParameterExpressionCost {
  std::size_t allocated_bytes;
  std::uint64_t work;
};
// Straight-line missing arms are expression evaluations, not arbitrary effect
// regions. The verifier owns conditionality, use closure, parameter scope and
// exact caller exits; discovery never grants permission to migrate an event.
class ParameterExpressionProof {
 public:
  static std::variant<ParameterExpressionProof, ParameterExpressionFailure>
  Build(const analysis::ValueProgram&, const ParameterPlan&,
        const DefaultCandidates&, std::uint32_t length,
        analysis::SemanticBudget = {});
  ParameterExpressionProof(ParameterExpressionProof&&) noexcept = default;
  ParameterExpressionProof& operator=(ParameterExpressionProof&&) noexcept =
      default;
  const ParameterExpressionCertificate& Evidence() const { return evidence_; }
  auto Defaults() const { return std::span(evidence_.defaults); }
  auto Operations() const { return std::span(evidence_.events); }
  auto BodyEntry() const { return evidence_.defaults.back().join; }
  auto Revision() const { return evidence_.revision; }
  auto ControlRevision() const { return evidence_.cfg_revision; }
  auto AllocatedBytes() const { return cost_.allocated_bytes; }
  auto Work() const { return cost_.work; }

 private:
  ParameterExpressionProof() = default;
  ParameterExpressionCertificate evidence_;
  ParameterExpressionCost cost_{};
};
std::variant<ParameterExpressionCost, ParameterExpressionFailure>
VerifyParameterExpressions(const analysis::ValueProgram&, const ParameterPlan&,
                           const ParameterExpressionCertificate&,
                           analysis::SemanticBudget = {});
}  // namespace abcd::source
#endif
