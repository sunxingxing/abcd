#ifndef ABCD_TOOLS_EXCEPTION_INSPECT_H_
#define ABCD_TOOLS_EXCEPTION_INSPECT_H_
#include <ostream>
#include "abcd/abc/method_body.h"
#include "abcd/source/linear_body.h"
int InspectExceptionFlow(const abcd::abc::MethodBody& body,
    abcd::abc::ProfileId profile, std::uint64_t frame_size,
    std::uint32_t first_abi_register,
    const abcd::source::MethodConstants& constants, std::ostream& output);
#endif
