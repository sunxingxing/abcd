#ifndef ABCD_ANALYSIS_CAPTURE_EFFECT_H_
#define ABCD_ANALYSIS_CAPTURE_EFFECT_H_

#include "abcd/abc/method_body.h"

namespace abcd::analysis {
// Same immutable body/profile/file evidence: this creation and its descendants
// cannot write any cell in their incoming ordinary lexical environment chain.
// This is preservation, not proof of initialization or a particular value.
struct CapturePreservation {
  abc::CodeOffset pc;
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_CAPTURE_EFFECT_H_
