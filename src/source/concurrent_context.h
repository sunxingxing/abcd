#ifndef ABCD_SOURCE_CONCURRENT_CONTEXT_H_
#define ABCD_SOURCE_CONCURRENT_CONTEXT_H_
#include "abcd/source/concurrent_failure.h"
#include "abcd/source/source_contract.h"
namespace abcd::source {
struct ConcurrentMetadata {
  // Kind metadata gives concurrency, not async flavor. Raw AsyncEnter only
  // selects the owner; async normalization must still prove the full protocol.
  bool asynchronous = false;
  abc::ModuleInfo module;
  abc::ConcurrentRequests requests;
  ConcurrentFailure location;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
std::variant<ConcurrentMetadata,ConcurrentFailure> ReadConcurrentCreation(
    const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
    const abc::MethodInfo&, const analysis::FunctionCreationSite&,
    analysis::FunctionSiteId, analysis::SemanticBudget);
// Evidence-only entry retained for diagnostic callers; never grants a body.
ConcurrentFailure InspectConcurrentCreation(const corpus::AbcFile&, const abc::Header&,
    const abc::MetadataIndex&, const abc::MethodInfo&, const analysis::FunctionCreationSite&,
    analysis::FunctionSiteId, SourceContract, analysis::SemanticBudget);
}
#endif
