#ifndef ABCD_SOURCE_ENTRY_COPY_ELISION_H_
#define ABCD_SOURCE_ENTRY_COPY_ELISION_H_

#include "abcd/source/semantic_source.h"
#include "entry_bindings.h"

namespace abcd::source {
// Reject any path that can print register identifiers outside bound reads.
// Declaration removal needs a separate proof; this only omits initialization.
bool CanElideEntryCopies(const SourceAst& ast);
bool IsBoundEntryCopy(const SourceStatement& statement,
                      std::span<const EntryBinding> bindings);
}  // namespace abcd::source
#endif
