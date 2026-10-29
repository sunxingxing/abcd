#ifndef ABCD_SOURCE_CALLER_EXIT_H_
#define ABCD_SOURCE_CALLER_EXIT_H_
#include <optional>
#include <vector>
#include "exception_forwarding.h"

namespace abcd::source {
// Whether an operation of a phased graph throws only to the caller, or to a
// local handler whose entry just rethrows its catch value unchanged (an
// environment cleanup `poplexenv; throw`), which is equivalent. Normal graphs
// have no local handlers. Forwarding summaries and catch values are computed
// once, on the first operation with a local handler; each query is
// O(log exits). Budget failures throw ForInFailure (resource or work limit).
class CallerExits {
 public:
  explicit CallerExits(const analysis::ValueProgram& graph) : graph_(graph) {}
  bool operator()(analysis::OpId op, analysis::SemanticBudget& budget);
 private:
  const analysis::ValueProgram& graph_;
  std::optional<ExceptionForwarding> forwarding_;
  std::vector<std::optional<analysis::ValueId>> catches_;
};
}  // namespace abcd::source
#endif
