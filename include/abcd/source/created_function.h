#ifndef ABCD_SOURCE_CREATED_FUNCTION_H_
#define ABCD_SOURCE_CREATED_FUNCTION_H_

#include "abcd/abc/method_target.h"
#include "abcd/source/base_function.h"

namespace abcd::source {
enum class CreatedFunctionError {
  kMissingCode,
  kName,
  kResourceLimit,
  kModuleOwner
};
using CreatedFunctionResult =
    std::variant<std::size_t, CreatedFunctionError, FunctionEmitError,
                 EmitError, BodyEmitFailure, analysis::FlowFailure,
                 analysis::EnvironmentFlowFailure,
                 analysis::LinearLexicalFailure, analysis::CreationError,
                 abc::MethodTargetError, abc::MetadataError,
                 abc::MethodAttributeError, abc::FrameError, abc::BodyFailure>;

// Same unchanged file/header/index/lookup and a decoded creation from caller
// required. Uses pinned 7.0 name/ABI semantics; container profile alone does
// not prove historical execution equivalence. Resolves, decodes and emits only
// the target, not caller control flow or records. Captures/home uses are
// refused by the body writer. Each emitted expression retains fresh runtime
// identity. budget.bytes includes decoded target and source scratch; caller
// admits index, lookup, file and constant-string storage. Partial-output rules
// match WriteBaseFunction: final publication must be atomic.
// A supplied module belongs to the caller's record; a target in another
// record is rejected before it can read a foreign import environment.
[[nodiscard]] CreatedFunctionResult WriteCreatedFunction(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const abc::DecodedInstruction& instruction,
    std::ostream& output, LocalLexicalBudget budget = {},
    abc::StringBudget strings = {}, const abc::ModuleInfo* module = nullptr);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_CREATED_FUNCTION_H_
