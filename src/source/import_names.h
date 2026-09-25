#ifndef ABCD_SOURCE_IMPORT_NAMES_H_
#define ABCD_SOURCE_IMPORT_NAMES_H_

#include "abcd/source/module_imports.h"
#include <string>
#include <vector>

namespace abcd::source {
struct ImportNames {
  std::vector<std::string> slots;
  std::vector<std::string_view> sorted;
};
// Reject collisions with all same-record runtime callable/class names before
// any source is emitted. Formal collisions are renamed by the function owner.
std::variant<ImportNames, ModuleImportFailure> RecoverImportNames(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::ModuleInfo& module, std::span<const abc::MethodInfo> methods,
    std::size_t bytes, abc::StringBudget strings);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_IMPORT_NAMES_H_
