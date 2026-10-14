#ifndef ABCD_SOURCE_EXCEPTION_REGION_FAILURE_H_
#define ABCD_SOURCE_EXCEPTION_REGION_FAILURE_H_
#include "abcd/analysis/exception_regions.h"
namespace abcd::source {
// Original and working boundaries are separate; absent provenance is not PC 0.
struct ExceptionRegionEvidence {
  analysis::TryId region;
  std::optional<abc::CodeOffset> body_pc, handler_pc;
  abc::CodeOffset working_body_pc, working_handler_pc;
  bool operator==(const ExceptionRegionEvidence&) const = default;
};
} // namespace abcd::source
#endif
