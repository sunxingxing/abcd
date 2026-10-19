#ifndef ABCD_SOURCE_CLASS_INITIALIZATION_PORTS_H_
#define ABCD_SOURCE_CLASS_INITIALIZATION_PORTS_H_
#include "original_flow.h"
namespace abcd::source {
struct RecoveryFlow;
struct FlowOriginalAction;
// A later, private cache write materializes the already initialized class;
// it cannot stand in for the raw inner Commit.
void VerifyClassInitializationCache(const RecoveryFlow&,
                                    const OriginalClassInitializationPort&,
                                    const FlowOriginalAction&,
                                    TreeCheckBudget&);
// Fresh original graph is required when retaining ports. Subsequent proposals
// recheck those immutable roles against native class events and physical
// writes.
void RetainClassInitializationPorts(const analysis::ValueProgram&,
                                    const FunctionTree&, OriginalFlow&,
                                    TreeCheckBudget&);
void VerifyClassInitializationRecovery(const FunctionTree&, const RecoveryFlow&,
                                       TreeCheckBudget&);
}  // namespace abcd::source
#endif
