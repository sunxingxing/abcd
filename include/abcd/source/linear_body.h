#ifndef ABCD_SOURCE_LINEAR_BODY_H_
#define ABCD_SOURCE_LINEAR_BODY_H_

#include "abcd/abc/method_body.h"
#include "abcd/abc/literal_array.h"
#include "abcd/abc/module_info.h"
#include "abcd/abc/string_entity.h"
#include "abcd/source/global_reference.h"
#include "abcd/source/scalar_expression.h"

namespace abcd::abc {
class MethodLookup;
}
namespace abcd::analysis {
class MethodRoleIndex;
struct AwaitSite;
}

namespace abcd::source {
class CreationExpansion;
inline constexpr std::uint64_t kDefaultMethodWork = 100000000;
enum class BodyEmitError {
  kExceptionRegions,
  kUnsupportedInstruction,
  kAccumulatorHole,
  kMissingTerminator,
  kTrailingInstructions,
  kExpression,
  kMissingConstants,
  kConstant,
  kIndexedName,
  kCreation,
  kResourceLimit,
  kWorkLimit,
  kMissingModule,
  kModuleBinding,
  kGlobalScope,
  kGlobalReference,
  kCurrentFunctionBinding,
  kInternalValue
};
enum class ObjectBufferError {
  kShape,
  kKey,
  kValue,
  kDuplicateKey,
  kResourceLimit
};
struct BodyEmitFailure {
  BodyEmitError kind;
  abc::CodeOffset offset;
  std::optional<analysis::ScalarError> scalar_error;
  std::optional<EmitError> emit_error;
  std::optional<abc::MetadataError> metadata_error = {};
  std::optional<abc::LiteralFailure> literal_error = {};
  std::optional<ObjectBufferError> object_error = {};
};
using BodyEmitResult = std::variant<std::size_t, BodyEmitFailure>;
struct ObjectBufferBudget {
  std::size_t bytes = 16 * 1024 * 1024;
};

enum class BodyLayout : std::uint8_t { kOriginal, kNormalizedAsync };
struct MethodConstants {
  MethodConstants(const corpus::AbcFile& input, const abc::Header& profile,
                  const abc::MetadataIndex& metadata, abc::EntityOffset target,
                  abc::StringBudget strings,
                  const abc::ModuleInfo* owner_module = nullptr,
                  const analysis::MethodRoleIndex* method_roles = nullptr,
                  std::optional<GlobalScope> scope = {},
                  std::span<const std::string_view> parameters = {})
      : file(input), header(profile), index(metadata), method(target),
        string_budget(strings), module(owner_module), roles(method_roles),
        global_scope(scope), source_parameters(parameters) {}
  const corpus::AbcFile& file;
  const abc::Header& header;
  const abc::MetadataIndex& index;
  abc::EntityOffset method;
  abc::StringBudget string_budget;
  // Caller-proven same-record module. Null keeps standalone function emission
  // from inventing a lexical import binding.
  const abc::ModuleInfo* module = nullptr;
  const analysis::MethodRoleIndex* roles = nullptr;
  std::optional<GlobalScope> global_scope;
  // Caller-proven source-formal names, in ABI parameter order; empty falls
  // back to private $pN bindings without guessing from method-table names.
  std::span<const std::string_view> source_parameters;
  // Lookup capability alone allows one level. Only a synchronous, bounded
  // expansion context may propagate it to created-function descendants.
  const abc::MethodLookup* leaf_methods = nullptr;
  CreationExpansion* expansion = nullptr;
  // Optional owner-verified normalized-body index -> original ABC PC mapping.
  // Borrowed only for synchronous emission; child methods never inherit it.
  std::span<const abc::CodeOffset> source_pcs;
  // Native async owner only: normalized await sites with original region
  // evidence. Borrowed alongside source_pcs, never inherited by child methods.
  std::span<const analysis::AwaitSite> awaits;
  // kNormalizedAsync requires successful ProveAsyncCompletion on this body:
  // rebased user handlers, private state and rejected-await register proof.
  // Never inherit this owner premise when expanding a child method.
  BodyLayout body_layout = BodyLayout::kOriginal;
};

// Emits statements inside a function, not a wrapper or record. Requires a
// validated decoded method. Caller supplies distinct writable local bindings
// for all registers and accumulator, initializes registers according to ABI,
// and guarantees language values (no VM sentinels). Accumulator reads must be
// dominated by a write here; caller initialization cannot bypass that check.
// Only straight-line scalar/string/property reads, strict named stores,
// fixed/narrow-range ordinary calls, proven same-block named invocations, nop,
// return and unconditional throw are supported. Named-call fusion requires
// private temporaries inaccessible to called code; no external catch may
// observe their intermediate state on abrupt exit. Exceptions, branches and trailing code are
// explicit errors, as are canonical element names on named property operations
// whose pinned fast path is not equivalent to native JS indexed access.
// Full preflight leaves output untouched on semantic/budget/binding failure;
// IO failure may leave a prefix. String loads
// require same-file constants and one transient string allowance within the
// process memory budget. Work is O(instructions * log(regions) + visited string
// bytes + emitted bytes), with no body-sized output buffer. Inputs must stay
// stable across both passes; second-pass read failures can leave a prefix and
// require atomic output. A null MethodConstants is a caller assertion of
// pinned v7 execution semantics for version-sensitive instructions.
[[nodiscard]] BodyEmitResult WriteLinearBody(
    const abc::MethodBody& body, ScalarNames names, std::ostream& output,
    EmitBudget budget = {}, const MethodConstants* constants = nullptr,
    ObjectBufferBudget object_budget = {});
}  // namespace abcd::source
#endif  // ABCD_SOURCE_LINEAR_BODY_H_
