#ifndef ABCD_SOURCE_EXCEPTION_STAGE_RECOVERY_H_
#define ABCD_SOURCE_EXCEPTION_STAGE_RECOVERY_H_
#include "recovery_flow.h"
namespace abcd::source {
void VerifyExceptionStageRecovery(const RecoveryFlow&, TreeCheckBudget&);
}
#endif
