#ifndef ABCD_SOURCE_GENERATOR_DELEGATE_H_
#define ABCD_SOURCE_GENERATOR_DELEGATE_H_
#include "abcd/abc/frame_layout.h"
#include "abcd/analysis/yield_region.h"
#include "abcd/abc/metadata_index.h"
#include "abcd/source/function_emit_error.h"

namespace abcd::source {
struct GeneratorNames {
  const corpus::AbcFile& file;
  const abc::Header& header;
  const abc::MetadataIndex& index;
  abc::EntityOffset method;
};
struct GeneratorDelegate {
  analysis::YieldRegion origin;
  std::size_t begin, end;
  abc::CodeOffset close_begin, close_end, close_catch, close_catch_end;
};
// Exact pinned OH7 synchronous YieldStar/Iterator.Close. String operands are
// resolved from this file's regional pool, not assumed from IC slots or IDs.
// No injected finalizer route is erased; unmatched variants remain unsupported.
[[nodiscard]] std::variant<GeneratorDelegate, FunctionEmitError, abc::MetadataError> ParseGeneratorDelegate(
    const abc::MethodBody& body, std::size_t begin, abc::RegisterId state,
    const abc::FrameLayout& frame, const GeneratorNames& names, std::size_t bytes);
}
#endif
