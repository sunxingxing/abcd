#ifndef ABCD_SOURCE_MODULE_IMPORTS_H_
#define ABCD_SOURCE_MODULE_IMPORTS_H_

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <variant>

#include "abcd/abc/module_info.h"
#include "abcd/abc/string_entity.h"
#include "abcd/source/emission.h"

namespace abcd::source {

enum class ModuleImportError {
  kRequestIndex,
  kString,
  kResourceLimit,
  kOutputLimit,
  kWrite
};
struct ModuleImportFailure {
  ModuleImportError kind;
  abc::EntityOffset offset;
  std::optional<abc::MetadataError> metadata;
};
using ModuleImportResult = std::variant<std::size_t, ModuleImportFailure>;

// Caller supplies module and header from the same unchanged ABC file. Emit
// this prelude in the record module before bodies that use $import<slot>.
// v7.0 module-environment slots are regular imports, then namespace imports;
// request-only declarations retain evaluation order. Indirect and star
// exports use native ESM reexports to preserve live-binding resolution.
// Semantic failures leave
// output untouched; IO failure can leave a partial write.
[[nodiscard]] ModuleImportResult WriteModuleImports(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::ModuleInfo& module, std::ostream& output,
    EmitBudget budget = {}, abc::StringBudget strings = {});

}  // namespace abcd::source

#endif  // ABCD_SOURCE_MODULE_IMPORTS_H_
