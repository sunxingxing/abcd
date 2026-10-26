#ifndef ABCD_ANALYSIS_EXCEPTION_THROW_FAILURE_H_
#define ABCD_ANALYSIS_EXCEPTION_THROW_FAILURE_H_
#include <cstdint>

namespace abcd::analysis {
enum class ExceptionThrowDomainError : std::uint8_t {
  kInput, kGuard, kPhi, kActivation, kCoverage, kResourceLimit, kWorkLimit
};
struct ExceptionThrowDomainFailure {
  ExceptionThrowDomainError kind;
};
}  // namespace abcd::analysis
#endif
