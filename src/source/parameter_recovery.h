#ifndef ABCD_SOURCE_PARAMETER_RECOVERY_H_
#define ABCD_SOURCE_PARAMETER_RECOVERY_H_
#include "default_signature.h"
#include "recovery_flow.h"
namespace abcd::source {
std::variant<DefaultSignatureProof, DefaultSignatureError> RetainParameterDefaults(
    const DefaultBody&, const FunctionTree&, TreeFlow&, const DefaultSignatureContext&,
    analysis::SemanticBudget);
// Recompute immutable input and actual signature before every recovery trial.
void VerifyParameterDefaultInput(const FunctionTree&, const OriginalParameterDefaults&, TreeCheckBudget&);
void VerifyParameterDefaultFlow(const FunctionTree&, const RecoveryFlow&, TreeCheckBudget&);
}
#endif
