#ifndef ABCD_SOURCE_CONCURRENT_COMPLETION_H_
#define ABCD_SOURCE_CONCURRENT_COMPLETION_H_
#include "abcd/source/concurrent_failure.h"
#include "abcd/analysis/value_ir.h"
namespace abcd::source {
struct ConcurrentReturn {
  analysis::OpId notification, returned;
  analysis::UseId value;
  bool operator==(const ConcurrentReturn&) const = default;
};
// A notification is an ordered host event. These ports bind it to the native
// return completion, never to a shared expression or an effect-free anchor.
struct ConcurrentCompletion {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<ConcurrentReturn> returns;
  std::vector<std::optional<std::uint32_t>> members; // notification OpId -> return port
  std::vector<std::uint8_t> uses; // only the proved notification result read
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
[[nodiscard]] std::variant<ConcurrentCompletion, ConcurrentFailure> BuildConcurrentCompletion(
    const analysis::ValueProgram&, analysis::SemanticBudget = {});
// Re-derives value identity, path counts and effect/completion order from the
// immutable graph; supplied ports are compared only after this analysis.
[[nodiscard]] std::variant<std::uint64_t, ConcurrentFailure> VerifyConcurrentCompletion(
    const analysis::ValueProgram&, const ConcurrentCompletion&, analysis::SemanticBudget = {});
}
#endif
