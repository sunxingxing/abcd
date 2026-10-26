#ifndef ABCD_SOURCE_ENVIRONMENT_RENEWAL_BINDING_H_
#define ABCD_SOURCE_ENVIRONMENT_RENEWAL_BINDING_H_
#include "abcd/analysis/environment_renewal.h"
#include "cell_plan.h"
#include "tree_checks.h"
namespace abcd::source {
// These queries are only evidence lookups. Every consumer independently
// verifies the immutable analysis certificate before using a lookup result.
const analysis::EnvironmentReset* RenewalDeclaration(
    const analysis::ValueProgram&, const AllocatedCell&, analysis::OpId);
struct OriginalFlow;
void VerifyRenewalBindings(const FunctionTree&, const OriginalFlow&,
                           TreeCheckBudget&);
void VerifyRenewalInput(const analysis::ValueProgram&, TreeCheckBudget&);
}  // namespace abcd::source
#endif
