#ifndef ABCD_TEST_SUPPORT_ANALYSIS_NUMERIC_PROOF_H_
#define ABCD_TEST_SUPPORT_ANALYSIS_NUMERIC_PROOF_H_
#include "abcd/analysis/semantic_validation.h"
#include "analysis/ssa_fixture.h"

namespace abcd::test {
// Pinned-runtime facts belong to analysis, never to a second source target.
inline analysis::SemanticProofResult NumericProof(
    const abc::MethodBody& body, abc::ProfileId profile,
    const abc::FrameLayout& frame,
    analysis::ArithmeticSemantics arithmetic = analysis::ArithmeticSemantics::kPinnedRuntime) {
  const auto lifted = analysis::BuildSemanticMethod(body, profile, frame.registers, {}, {}, &frame);
  Check(std::holds_alternative<analysis::SemanticMethod>(lifted));
  return analysis::VerifySemanticSource(body, std::get<analysis::SemanticMethod>(lifted),
      frame.registers, frame.locals, {}, analysis::CapturedValues::kReject, {},
      analysis::ClosureWrites::kUnknown, {}, analysis::CallResultValues::kLanguage,
      analysis::FrameReceiver::kUnknown, nullptr, arithmetic);
}
inline void CheckPinnedNumericRefusal(const abc::MethodBody& body, abc::ProfileId profile,
                                     const abc::FrameLayout& frame,
                                     analysis::ArithmeticSemantics arithmetic = analysis::ArithmeticSemantics::kPinnedRuntime) {
  Check(std::get<analysis::SemanticProofFailure>(NumericProof(body, profile, frame, arithmetic)).kind ==
      analysis::SemanticProofError::kInternalValue);
}
}  // namespace abcd::test
#endif
