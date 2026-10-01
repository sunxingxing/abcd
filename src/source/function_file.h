#ifndef ABCD_SOURCE_FUNCTION_FILE_H_
#define ABCD_SOURCE_FUNCTION_FILE_H_

#include "function_values.h"
#include "module_reads.h"
#include "abcd/source/record_module_context.h"

namespace abcd::source {
struct FileFunction {
  abc::MethodBody body;
  abc::FrameLayout frame;
  FunctionInput input;
  FunctionValues values;
  std::size_t allocated_bytes = 0;
  std::optional<ModuleReads> module_reads;
};
enum class FunctionFileError : std::uint8_t { kIdentity, kMethod, kChanged, kResourceLimit };
using FunctionFileFailure = std::variant<FunctionFileError, abc::BodyFailure,
    abc::MetadataError, abc::FrameError, FunctionInputFailure, FunctionValueFailure,
    ModuleReadFailure>;
// Same unchanged file/header/index/identity, with a validated method position.
// Reads the ABI and body itself and retains original input alongside working
// values for creation/capture binding. No sidecars, receipts or source claims.
// File/index storage is caller-admitted. This allowance includes retained body,
// input and values plus each preparation pass's scratch. Existing input/value
// passes have per-pass work caps, not one cumulative family work counter.
// An optional caller-owned record context resolves module accesses without
// repeating file packaging/record discovery for each family member. Its storage
// is caller-admitted. Absent context leaves module binding obligations open.
[[nodiscard]] std::variant<FileFunction, FunctionFileFailure> ReadFunctionValues(
    const corpus::AbcFile& file, const abc::Header& header, const abc::MetadataIndex& index,
    abc::MethodPosition method, RouteInputIdentity identity, InputCaptureMode mode,
    analysis::SemanticBudget budget = {}, const RecordModuleContext* modules = nullptr,
    const FunctionContext* owner = nullptr, const ParentModuleInput* modules_input = nullptr);
}  // namespace abcd::source
#endif
