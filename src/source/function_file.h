#ifndef ABCD_SOURCE_FUNCTION_FILE_H_
#define ABCD_SOURCE_FUNCTION_FILE_H_

#include "function_values.h"

namespace abcd::source {
struct FileFunction {
  abc::MethodBody body;
  abc::FrameLayout frame;
  FunctionInput input;
  FunctionValues values;
  std::size_t allocated_bytes = 0;
};
enum class FunctionFileError : std::uint8_t { kIdentity, kMethod, kChanged, kResourceLimit };
using FunctionFileFailure = std::variant<FunctionFileError, abc::BodyFailure,
    abc::MetadataError, abc::FrameError, FunctionInputFailure, FunctionValueFailure>;
// Same unchanged file/header/index/identity, with a validated method position.
// Reads the ABI and body itself and retains original input alongside working
// values for creation/capture binding. No sidecars, receipts or source claims.
// File/index storage is caller-admitted. This allowance includes retained body,
// input and values plus each preparation pass's scratch. Existing input/value
// passes have per-pass work caps, not one cumulative family work counter.
[[nodiscard]] std::variant<FileFunction, FunctionFileFailure> ReadFunctionValues(
    const corpus::AbcFile& file, const abc::Header& header, const abc::MetadataIndex& index,
    abc::MethodPosition method, RouteInputIdentity identity, InputCaptureMode mode,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
