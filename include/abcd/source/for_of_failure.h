#ifndef ABCD_SOURCE_FOR_OF_FAILURE_H_
#define ABCD_SOURCE_FOR_OF_FAILURE_H_
#include <cstdint>
namespace abcd::source {
// Stable typed causes for iterator source correspondence. The analysis keeps
// its independent runtime facts; this enum never grants source admission.
enum class ForOfIterationError : std::uint8_t {
  kInput, kIdentity, kEntry, kAdvance, kDone, kBinding, kActivation,
  kCorrespondence, kAcquisitionSemantics, kResourceLimit, kWorkLimit
};
}
#endif
