#ifndef ABCD_SOURCE_MODULE_EXPORTS_H_
#define ABCD_SOURCE_MODULE_EXPORTS_H_

#include <iosfwd>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "abcd/abc/module_info.h"
#include "abcd/abc/method_body.h"
#include "abcd/abc/string_entity.h"
#include "abcd/analysis/module_store.h"
#include "abcd/source/emission.h"

namespace abcd::source {
enum class ModuleExportError {
  kProfile,
  kResourceLimit,
  kDuplicateExport,
  kSlot,
  kOutputLimit,
  kWrite
};
struct ModuleExportBudget {
  std::size_t bytes = 16 * 1024 * 1024;
  abc::StringBudget strings;
};
// Wire slot grouping of a record's local exports, with duplicate-export
// validation. Printing exports belongs to the value-tree record printer.
class ModuleExports {
 public:
  [[nodiscard]] std::size_t slot_count() const { return slot_count_; }

 private:
  friend std::variant<ModuleExports, ModuleExportError, abc::MetadataError>
  BuildModuleExports(const corpus::AbcFile&, const abc::Header&,
                     const abc::ModuleInfo&, ModuleExportBudget);
  struct Alias {
    analysis::ModuleSlot slot;
    std::u16string exported;
  };
  std::vector<Alias> aliases_;
  std::size_t slot_count_ = 0;
};
using ModuleExportResult =
    std::variant<ModuleExports, ModuleExportError, abc::MetadataError>;
// All supported dynamic profiles use adjacent-equal local export names to
// share a VM slot. Nonadjacent repeats remain distinct slots, not a name map.
[[nodiscard]] ModuleExportResult BuildModuleExports(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::ModuleInfo& module, ModuleExportBudget budget = {});
}  // namespace abcd::source

#endif  // ABCD_SOURCE_MODULE_EXPORTS_H_
