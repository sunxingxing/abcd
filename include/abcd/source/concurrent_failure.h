#ifndef ABCD_SOURCE_CONCURRENT_FAILURE_H_
#define ABCD_SOURCE_CONCURRENT_FAILURE_H_
#include "abcd/abc/concurrent_requests.h"
#include "abcd/abc/record_attributes.h"
#include "abcd/analysis/function_creation_sites.h"
namespace abcd::source {
enum class ConcurrentError : std::uint8_t {
  kOutputPolicy, kPreparationGate, kOwner, kModule, kRequests, kResourceLimit, kWorkLimit
};
// Fixed-size witness. Arbitrary request lists remain in the separately budgeted
// metadata reader; their annotation identity/count is sufficient to locate them.
// The child has no body failure PC here. The parent's creation PC is separate.
struct ConcurrentFailure {
  ConcurrentError kind = ConcurrentError::kPreparationGate;
  abc::EntityOffset method{}, record{}, creator{};
  analysis::FunctionSiteId occurrence{};
  abc::CodeOffset creation_pc{};
  std::optional<abc::EntityOffset> annotation;
  std::optional<bool> annotation_present;
  std::optional<std::uint32_t> request_count;
  std::optional<std::uint32_t> element, request;
  std::variant<std::monostate, abc::ConcurrentRequestError, abc::MetadataError,
      abc::AttributeError, abc::ModuleError> cause;
  std::optional<abc::MetadataError> metadata;
  bool operator==(const ConcurrentFailure&) const = default;
};
}
#endif
