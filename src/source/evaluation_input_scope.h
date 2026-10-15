#ifndef ABCD_SOURCE_EVALUATION_INPUT_SCOPE_H_
#define ABCD_SOURCE_EVALUATION_INPUT_SCOPE_H_
#include "abcd/source/evaluation_plan_cause.h"
#include <exception>
namespace abcd::source {
// Capture the innermost owned check only on an actual failure. Successful
// iterations cannot leave a stale PC on a later graph-wide refusal. No TLS,
// allocation or diagnostic-dependent admission; callers may omit the sink.
class EvaluationInputScope {
 public:
  EvaluationInputScope(EvaluationInputSite* sink, EvaluationInputSite site)
      : sink_(sink), site_(site), exceptions_(std::uncaught_exceptions()) {}
  ~EvaluationInputScope() {
    if (sink_ && !sink_->check && std::uncaught_exceptions() > exceptions_) *sink_ = site_;
  }
 private:
  EvaluationInputSite* sink_;
  EvaluationInputSite site_;
  int exceptions_;
};
} // namespace abcd::source
#endif
