#ifndef ABCD_ABC_CONCURRENT_REQUESTS_H_
#define ABCD_ABC_CONCURRENT_REQUESTS_H_
#include "abcd/abc/module_info.h"
#include <span>
namespace abcd::abc {
enum class ConcurrentRequestError : std::uint8_t {
  kOwner, kAnnotation, kElementName, kElementType, kRequest, kRead, kResourceLimit, kWorkLimit
};
struct ConcurrentRequestFailure {
  ConcurrentRequestError kind;
  EntityOffset method{}, record{};
  std::optional<EntityOffset> annotation;
  std::optional<std::uint32_t> element, request;
  std::optional<MetadataError> metadata;
};
struct ConcurrentRequest {
  EntityOffset annotation;
  std::uint32_t element;
  ModuleRequestId request;
  EntityOffset specifier;
};
struct ConcurrentRequests {
  bool present = false;
  std::vector<ConcurrentRequest> entries;
  std::optional<EntityOffset> first_annotation;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
struct ConcurrentRequestBudget { std::size_t bytes=64*1024*1024; std::uint64_t work=8*1024*1024; };
// Validated same-file method and record module table. No string/name planner.
// Two streaming passes, O(annotation tags + expanded elements + log regions per
// annotation); ordered duplicates are retained, absent is distinct from empty.
[[nodiscard]] std::variant<ConcurrentRequests, ConcurrentRequestFailure> ReadConcurrentRequests(
    const corpus::AbcFile&, const Header&, const MetadataIndex&, const MethodInfo&,
    EntityOffset record, std::span<const EntityOffset> requests, ConcurrentRequestBudget = {});
}
#endif
