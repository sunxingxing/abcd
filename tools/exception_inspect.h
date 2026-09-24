#ifndef ABCD_TOOLS_EXCEPTION_INSPECT_H_
#define ABCD_TOOLS_EXCEPTION_INSPECT_H_
#include <ostream>
#include "abcd/abc/method_body.h"
int InspectExceptionFlow(const abcd::abc::MethodBody& body,
    abcd::abc::ProfileId profile, std::uint64_t frame_size, std::ostream& output);
#endif
