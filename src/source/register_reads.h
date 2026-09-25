#ifndef ABCD_SOURCE_REGISTER_READS_H_
#define ABCD_SOURCE_REGISTER_READS_H_

#include "abcd/source/semantic_source.h"

namespace abcd::source {
// Unknown owner protocols are barriers, never evidence of an absent read.
enum class RegisterRead { kAbsent, kPresent, kUnknown };
RegisterRead ReadsRegister(const SourceExpression& expression,
                           abc::RegisterId reg);
bool ReadsRegister(const analysis::ScalarValue& value, abc::RegisterId reg);
}  // namespace abcd::source
#endif
