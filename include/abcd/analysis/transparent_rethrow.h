#ifndef ABCD_ANALYSIS_TRANSPARENT_RETHROW_H_
#define ABCD_ANALYSIS_TRANSPARENT_RETHROW_H_

#include "abcd/abc/method_body.h"

namespace abcd::analysis {
// Exception-transparent catch regions (semantic, not layout, normalization).
//
// A handler whose first instruction is THROW rethrows the caught exception
// unchanged: the pinned interpreter enters a handler with the exception in the
// accumulator. The rethrow re-enters dispatch at the handler PC. When EVERY
// try region of a method has only such handlers and NO region covers any
// handler PC, every exception leaves the method with the same value whether or
// not the regions exist, so erasing all of them preserves observable
// behaviour. A covered handler (self or mutual rethrow chain) keeps them all. Partially transparent methods are
// left unchanged, because erasing one region could redirect an exception to a
// later overlapping region.
//
// Provenance: es2panda PandaGen::AddCatchBlockForImplicitSuperCallChecks
// (ets_frontend compiler/core/pandagen.cpp:1286) wraps derived-constructor
// super-call checks in such regions; the rule itself is generic.
//
// After erasure, trailing handler instructions that no normal edge reaches
// (not a branch target, previous instruction does not fall through) are
// removed. Returns true if the body changed.
// O(instructions + (regions + handlers) log regions).
[[nodiscard]] bool EraseTransparentRethrowRegions(abc::MethodBody& body);
}  // namespace abcd::analysis

#endif  // ABCD_ANALYSIS_TRANSPARENT_RETHROW_H_
