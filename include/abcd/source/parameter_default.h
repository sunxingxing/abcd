#ifndef ABCD_SOURCE_PARAMETER_DEFAULT_H_
#define ABCD_SOURCE_PARAMETER_DEFAULT_H_

#include "abcd/analysis/register_ssa.h"

namespace abcd::source {
struct DefaultInteger {
  std::int32_t value;
  bool negated = false;
};
struct DefaultUndefined {};
// Original instruction interval includes the undefined test and its sole arm.
// Only the enclosing callable may consume this certificate as native syntax.
struct ParameterDefault {
  std::uint32_t parameter;
  analysis::InstructionId first;
  analysis::InstructionId branch;
  analysis::InstructionId end;
  std::variant<DefaultInteger, DefaultUndefined> value;
};
}  // namespace abcd::source
#endif
