#ifndef ABCD_SOURCE_EXCEPTION_REGION_FAILURE_H_
#define ABCD_SOURCE_EXCEPTION_REGION_FAILURE_H_
#include "abcd/analysis/exception_regions.h"
#include "abcd/analysis/value_ir.h"
#include <array>
namespace abcd::source {
// A normal exit is either a real original edge or a nested region summary.
// Continuation is the proven corridor endpoint; target is the raw edge target.
struct ExceptionExitEvidence {
  std::variant<analysis::EdgeId,analysis::TryId> source;
  std::optional<analysis::WorkBlockId> from;
  analysis::WorkBlockId target, continuation;
  std::optional<abc::CodeOffset> pc, working_pc, target_pc, target_working_pc,
      continuation_pc, continuation_working_pc;
  bool operator==(const ExceptionExitEvidence&) const = default;
};
// Original and working boundaries are separate; absent provenance is not PC 0.
struct ExceptionRegionEvidence {
  analysis::TryId region;
  std::optional<abc::CodeOffset> body_pc, handler_pc;
  abc::CodeOffset working_body_pc, working_handler_pc;
  std::optional<std::array<ExceptionExitEvidence,2>> exits;
  bool operator==(const ExceptionRegionEvidence&) const = default;
};
} // namespace abcd::source
#endif
