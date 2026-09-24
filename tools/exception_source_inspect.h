#ifndef ABCD_TOOLS_EXCEPTION_SOURCE_INSPECT_H_
#define ABCD_TOOLS_EXCEPTION_SOURCE_INSPECT_H_

#include <ostream>
#include "abcd/source/exception_source.h"

void InspectExceptionSource(const abcd::source::ExceptionAstInput& input,
    abcd::analysis::SemanticRegionTree regions,
    const abcd::source::MethodConstants& constants, std::uint64_t frame_size,
    std::size_t bytes, std::ostream& output,
    abcd::analysis::CapturedValues captured_values);
#endif
