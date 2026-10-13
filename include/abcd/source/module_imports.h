#ifndef ABCD_SOURCE_MODULE_IMPORTS_H_
#define ABCD_SOURCE_MODULE_IMPORTS_H_

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <variant>

#include "abcd/abc/module_info.h"
#include "abcd/abc/string_entity.h"
#include "abcd/source/emission.h"
#include "abcd/source/source_contract.h"

namespace abcd::source {

enum class ModuleImportError {
  kRequestIndex,
  kString,
  kResourceLimit,
  kOutputLimit,
  kWrite, kContract, kRequestPhases, kBinding, kModuleKind
};
struct ModuleImportFailure {
  ModuleImportError kind;
  abc::EntityOffset offset;
  std::optional<abc::MetadataError> metadata;
};
using ModuleImportResult = std::variant<std::size_t, ModuleImportFailure>;

// Caller supplies module and header from the same unchanged ABC file. Emit
// this prelude before bodies using the same slot names ($importN by default).
// v7.0 module-environment slots are regular imports, then namespace imports;
// declarations are emitted in request order without changing those slots.
// Requests without import bindings retain bare imports. Indirect and star
// exports use native ESM reexports to preserve live-binding resolution.
// Temporary directive capacity shares the caller-admitted strings allowance;
// string parsing receives the remainder. O(E log E + R + emitted text) for E
// import entries and R requests, O(E) temporary storage.
// Optional names is a record-owned, validated slot plan shared with every
// consumer. Empty entries keep $importN; nonempty entries must be distinct
// strict binding identifiers with proven source-scope collision handling.
// Semantic failures leave
// output untouched; IO failure can leave a partial write.
[[nodiscard]] ModuleImportResult WriteModuleImports(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::ModuleInfo& module, std::ostream& output,
    EmitBudget budget = {}, abc::StringBudget strings = {},
    std::span<const std::string> names = {}, SourceContract contract = kSourceContract);

}  // namespace abcd::source

#endif  // ABCD_SOURCE_MODULE_IMPORTS_H_
