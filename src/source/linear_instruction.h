#ifndef ABCD_SOURCE_LINEAR_INSTRUCTION_H_
#define ABCD_SOURCE_LINEAR_INSTRUCTION_H_

#include "abcd/analysis/named_invocation.h"
#include "abcd/source/linear_body.h"

namespace abcd::source {
BodyEmitResult WriteNamedInvocation(const analysis::NamedInvocation& invocation,
                                    abc::CodeOffset offset, ScalarNames names,
                                    std::ostream& output, EmitBudget budget,
                                    const MethodConstants* constants);
// PRIVATE: caller must preflight the complete method and prove this statement's
// reads are language-valued and acc is initialized. No standalone preflight or
// terminator validation here. Reuses scalar/constant/call emission verbatim.
BodyEmitResult WriteLinearInstruction(const abc::BodyInstruction& instruction,
                                      ScalarNames names, std::ostream& output,
                                      EmitBudget budget,
                                      const MethodConstants* constants,
                                      ObjectBufferBudget object_budget = {});
}  // namespace abcd::source
#endif  // ABCD_SOURCE_LINEAR_INSTRUCTION_H_
