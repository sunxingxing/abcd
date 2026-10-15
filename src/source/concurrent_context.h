#ifndef ABCD_SOURCE_CONCURRENT_CONTEXT_H_
#define ABCD_SOURCE_CONCURRENT_CONTEXT_H_
#include "abcd/source/concurrent_failure.h"
#include "abcd/source/source_contract.h"
namespace abcd::source {
// Evidence only. Always returns a located refusal; does not grant a callable,
// capture or notification certificate and cannot enable publication.
ConcurrentFailure InspectConcurrentCreation(const corpus::AbcFile&, const abc::Header&,
    const abc::MetadataIndex&, const abc::MethodInfo&, const analysis::FunctionCreationSite&,
    analysis::FunctionSiteId, SourceContract, analysis::SemanticBudget);
}
#endif
