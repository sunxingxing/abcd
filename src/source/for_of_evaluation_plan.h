#ifndef ABCD_SOURCE_FOR_OF_EVALUATION_PLAN_H_
#define ABCD_SOURCE_FOR_OF_EVALUATION_PLAN_H_
#include "for_of_events.h"
namespace abcd::source {
// Borrowed only for construction/verification. PhasedBody owns every referent;
// no source view or borrowed pointer is retained in the resulting plan.
struct ForOfEvaluationInput {
  const abc::MethodBody& body;
  analysis::PhasedValueInput input;
  const ExceptionPlan& regions;
  const ExceptionControl& control;
  const VerifiedProtocolRegionOwnership& ownership;
  SourceContract contract;
};
struct ForOfUseDisposition {
  std::optional<IteratorProtocolId> protocol;
  bool operator==(const ForOfUseDisposition&) const = default;
};
struct ForOfEvaluationPlan {
  ForOfIdentityIndex identity;
  ForOfIterationProof iteration;
  ForOfEvents events;
  std::vector<std::optional<ForOfMember>> operations, values;
  std::vector<ForOfUseDisposition> uses;
  // Ordinary source binding aliases, separately indexed from private protocol
  // values. Their storage survives a body evaluation, not the whole invocation.
  std::vector<std::optional<IteratorProtocolId>> bindings;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
[[nodiscard]] std::variant<ForOfEvaluationPlan,ForOfIterationFailure> BuildForOfEvaluationPlan(
    const analysis::ValueProgram&,const ForOfEvaluationInput&,analysis::SemanticBudget = {});
[[nodiscard]] std::variant<std::uint64_t,ForOfIterationFailure> VerifyForOfEvaluationPlan(
    const analysis::ValueProgram&,const ForOfEvaluationInput&,const ForOfEvaluationPlan&,analysis::SemanticBudget = {});
}
#endif
